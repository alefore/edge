#include "src/editor.h"

#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" {
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_argument_mode.h"
#include "src/editor_vm.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/language/wstring.h"
#include "src/server.h"
#include "src/substring.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/widget_list.h"

namespace afc::editor {
using concurrent::ThreadPool;
using concurrent::WorkQueue;
using infrastructure::AddSeconds;
using infrastructure::FileDescriptor;
using infrastructure::Now;
using infrastructure::Path;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::NonNull;
using language::Observers;
using language::Pointer;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

namespace gc = language::gc;

// Executes pending work from all buffers.
void EditorState::ExecutePendingWork() { work_queue_->Execute(); }

std::optional<struct timespec> EditorState::WorkQueueNextExecution() const {
  std::optional<struct timespec> output;
  for (auto& buffer : buffers_) {
    if (auto buffer_output = buffer.second.ptr()->work_queue()->NextExecution();
        buffer_output.has_value() &&
        (!output.has_value() || buffer_output.value() < output.value())) {
      output = buffer_output;
    }
  }
  return output;
}

const NonNull<std::shared_ptr<WorkQueue>>& EditorState::work_queue() const {
  return work_queue_;
}

ThreadPool& EditorState::thread_pool() { return thread_pool_; }

void EditorState::ResetInternalEventNotifications() {
  char buffer[4096];
  VLOG(5) << "Internal events detected.";
  while (read(fd_to_detect_internal_events().read(), buffer, sizeof(buffer)) >
         0)
    continue;
  has_internal_events_.lock([](bool& value) { value = false; });
}

void EditorState::NotifyInternalEvent() {
  VLOG(5) << "Internal event notification!";
  if (!has_internal_events_.lock([](bool& value) {
        bool old_value = value;
        value = true;
        return old_value;
      }) &&
      write(pipe_to_communicate_internal_events_.second.read(), " ", 1) == -1) {
    status_.SetWarningText(L"Write to internal pipe failed: " +
                           FromByteString(strerror(errno)));
  }
}

EditorState::EditorState(CommandLineValues args, audio::Player& audio_player)
    : string_variables_(editor_variables::StringStruct()->NewInstance()),
      bool_variables_(editor_variables::BoolStruct()->NewInstance()),
      int_variables_(editor_variables::IntStruct()->NewInstance()),
      double_variables_(editor_variables::DoubleStruct()->NewInstance()),
      work_queue_(WorkQueue::New()),
      thread_pool_(32, work_queue_.get_shared()),
      home_directory_(args.home_directory),
      edge_path_([](std::vector<std::wstring> paths) {
        std::vector<Path> output;
        for (auto& candidate : paths) {
          if (auto path = Path::FromString(candidate); !path.IsError()) {
            output.push_back(std::move(path.value()));
          }
        }
        return output;
      }(args.config_paths)),
      frames_per_second_(args.frames_per_second),
      environment_(BuildEditorEnvironment(*this)),
      default_commands_(NewCommandMode(*this)),
      pipe_to_communicate_internal_events_([] {
        int output[2];
        return pipe2(output, O_NONBLOCK) == -1
                   ? std::make_pair(FileDescriptor(-1), FileDescriptor(-1))
                   : std::make_pair(FileDescriptor(output[0]),
                                    FileDescriptor(output[1]));
      }()),
      audio_player_(audio_player),
      buffer_tree_(*this),
      status_(audio_player_) {
  work_queue_->OnSchedule().Add([this] {
    NotifyInternalEvent();
    return Observers::State::kAlive;
  });
  auto paths = edge_path();
  futures::ForEach(paths.begin(), paths.end(), [this](Path dir) {
    auto path = Path::Join(dir, Path::FromString(L"hooks/start.cc").value());
    ValueOrError<NonNull<std::unique_ptr<Expression>>> expression =
        CompileFile(ToByteString(path.read()), gc_pool_, environment_);
    if (expression.IsError()) {
      Error error =
          Error::Augment(path.read() + L": error: ", expression.error());
      LOG(INFO) << "Compilation error: " << error;
      status_.SetWarningText(error.description);
      return futures::Past(futures::IterationControlCommand::kContinue);
    }
    LOG(INFO) << "Evaluating file: " << path;
    return Evaluate(
               expression.value().value(), gc_pool_, environment_,
               [path, work_queue = work_queue()](std::function<void()> resume) {
                 LOG(INFO) << "Evaluation of file yields: " << path;
                 work_queue->Schedule(std::move(resume));
               })
        .Transform([](gc::Root<Value>) {
          // TODO(2022-04-26): Figure out a way to get rid of `Success`.
          return futures::Past(
              Success(futures::IterationControlCommand::kContinue));
        })
        .ConsumeErrors([](Error) {
          return futures::Past(futures::IterationControlCommand::kContinue);
        });
  });

  double_variables_.ObserveValue(editor_variables::volume).Add([this] {
    audio_player_.SetVolume(
        audio::Volume(max(0.0, min(1.0, Read(editor_variables::volume)))));
    return Observers::State::kAlive;
  });
}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  LOG(INFO) << "Closing buffers.";
  for (auto& buffer : buffers_) {
    buffer.second.ptr()->Close();
  }

  environment_.ptr()->Clear();  // We may have loops. This helps break them.
  buffers_.clear();

  gc_pool_.Reclaim();
}

const bool& EditorState::Read(const EdgeVariable<bool>* variable) const {
  return bool_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<bool>* variable, bool value) {
  bool_variables_.Set(variable, value);
}

void EditorState::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  Set(variable, modifiers().repetitions.has_value()
                    ? modifiers().repetitions != 0
                    : !Read(variable));
}

const wstring& EditorState::Read(const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<wstring>* variable, wstring value) {
  string_variables_.Set(variable, value);
  if (variable == editor_variables::buffer_sort_order) {
    AdjustWidgets();
  }
}

const int& EditorState::Read(const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<int>* variable, int value) {
  int_variables_.Set(variable, value);
  if (variable == editor_variables::buffers_to_retain ||
      variable == editor_variables::buffers_to_show) {
    AdjustWidgets();
  }
}

const double& EditorState::Read(const EdgeVariable<double>* variable) const {
  return double_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<double>* variable, double value) {
  double_variables_.Set(variable, value);
}

void EditorState::CheckPosition() {
  // TODO(2022-05-15, easy): Use VisitPointer.
  if (auto buffer = buffer_tree_.active_buffer(); buffer.has_value()) {
    buffer->ptr()->CheckPosition();
  }
}

void EditorState::CloseBuffer(OpenBuffer& buffer) {
  buffer.PrepareToClose().SetConsumer(
      [this, buffer = buffer.NewRoot()](PossibleError error) {
        if (error.IsError()) {
          buffer.ptr()->status().SetWarningText(
              L"🖝  Unable to close (“*ad” to ignore): " +
              error.error().description + L": " +
              buffer.ptr()->Read(buffer_variables::name));
          return;
        }

        buffer.ptr()->Close();
        buffer_tree_.RemoveBuffer(buffer.ptr().value());
        buffers_.erase(buffer.ptr()->name());
        AdjustWidgets();
      });
}

gc::Root<OpenBuffer> EditorState::FindOrBuildBuffer(
    BufferName name, std::function<gc::Root<OpenBuffer>()> callback) {
  if (auto it = buffers_.find(name); it != buffers_.end()) {
    return it->second;
  }
  gc::Root<OpenBuffer> value = callback();
  buffers_.insert_or_assign(name, value);
  return value;
}

void EditorState::set_current_buffer(gc::Root<OpenBuffer> buffer,
                                     CommandArgumentModeApplyMode apply_mode) {
  if (apply_mode == CommandArgumentModeApplyMode::kFinal) {
    buffer.ptr()->Visit();
  } else {
    buffer.ptr()->Enter();
  }
  buffer_tree_.GetActiveLeaf().SetBuffer(buffer.ptr().ToWeakPtr());
  AdjustWidgets();
}

void EditorState::SetActiveBuffer(size_t position) {
  set_current_buffer(
      buffer_tree_.GetBuffer(position % buffer_tree_.BuffersCount()),
      CommandArgumentModeApplyMode::kFinal);
}

void EditorState::AdvanceActiveBuffer(int delta) {
  if (buffer_tree_.BuffersCount() <= 1) return;
  delta += buffer_tree_.GetCurrentIndex();
  size_t total = buffer_tree_.BuffersCount();
  if (delta < 0) {
    delta = total - ((-delta) % total);
  } else {
    delta %= total;
  }
  set_current_buffer(buffer_tree_.GetBuffer(delta % total),
                     CommandArgumentModeApplyMode::kFinal);
}

void EditorState::AdjustWidgets() {
  buffer_tree_.SetBufferSortOrder(
      Read(editor_variables::buffer_sort_order) == L"last_visit"
          ? BuffersList::BufferSortOrder::kLastVisit
          : BuffersList::BufferSortOrder::kAlphabetic);
  auto buffers_to_retain = Read(editor_variables::buffers_to_retain);
  buffer_tree_.SetBuffersToRetain(buffers_to_retain >= 0
                                      ? size_t(buffers_to_retain)
                                      : std::optional<size_t>());
  auto buffers_to_show = Read(editor_variables::buffers_to_show);
  buffer_tree_.SetBuffersToShow(buffers_to_show >= 1 ? size_t(buffers_to_show)
                                                     : std::optional<size_t>());
  buffer_tree_.Update();
}

bool EditorState::has_current_buffer() const {
  return current_buffer().has_value();
}
std::optional<gc::Root<OpenBuffer>> EditorState::current_buffer() const {
  return buffer_tree_.active_buffer();
}

std::vector<gc::Root<OpenBuffer>> EditorState::active_buffers() const {
  std::vector<gc::Root<OpenBuffer>> output;
  if (status().GetType() == Status::Type::kPrompt) {
    output.push_back(status().prompt_buffer().value());
  } else if (Read(editor_variables::multiple_buffers)) {
    output = buffer_tree_.GetAllBuffers();
  } else if (std::optional<gc::Root<OpenBuffer>> buffer = current_buffer();
             buffer.has_value()) {
    if (buffer->ptr()->status().GetType() == Status::Type::kPrompt) {
      buffer = buffer->ptr()->status().prompt_buffer();
    }
    output.push_back(buffer.value());
  }
  return output;
}

void EditorState::AddBuffer(gc::Root<OpenBuffer> buffer,
                            BuffersList::AddBufferType insertion_type) {
  std::vector<gc::Root<OpenBuffer>> initial_active_buffers = active_buffers();
  buffer_tree().AddBuffer(buffer, insertion_type);
  AdjustWidgets();
  if (initial_active_buffers != active_buffers()) {
    // The set of buffers changed; if some mode was active, ... cancel it.
    // Perhaps the keyboard redirect should have a method to react to this, so
    // that it can decide what to do? Then again, does it make sense for any of
    // them to do anything other than be canceled?
    set_keyboard_redirect(nullptr);
  }
}

futures::Value<EmptyValue> EditorState::ForEachActiveBuffer(
    std::function<futures::Value<EmptyValue>(OpenBuffer&)> callback) {
  auto buffers = active_buffers();
  return futures::ForEachWithCopy(
             buffers.begin(), buffers.end(),
             [callback](const gc::Root<OpenBuffer>& buffer) {
               return callback(buffer.ptr().value()).Transform([](EmptyValue) {
                 return futures::IterationControlCommand::kContinue;
               });
             })
      .Transform([](futures::IterationControlCommand) { return EmptyValue(); });
}

futures::Value<EmptyValue> EditorState::ForEachActiveBufferWithRepetitions(
    std::function<futures::Value<EmptyValue>(OpenBuffer&)> callback) {
  auto value = futures::Past(EmptyValue());
  if (!modifiers().repetitions.has_value()) {
    value = ForEachActiveBuffer(callback);
  } else {
    gc::Root<OpenBuffer> buffer = buffer_tree().GetBuffer(
        (max(modifiers().repetitions.value(), 1ul) - 1) %
        buffer_tree().BuffersCount());
    value = callback(buffer.ptr().value());
  }
  return value.Transform([this](EmptyValue) {
    ResetModifiers();
    return EmptyValue();
  });
}

futures::Value<EmptyValue> EditorState::ApplyToActiveBuffers(
    transformation::Variant transformation) {
  return ForEachActiveBuffer(
      [transformation = std::move(transformation)](OpenBuffer& buffer) {
        return buffer.ApplyToCursors(transformation);
      });
}

BufferName GetBufferName(const wstring& prefix, size_t count) {
  return BufferName(prefix + L" " + std::to_wstring(count));
}

BufferName EditorState::GetUnusedBufferName(const wstring& prefix) {
  size_t count = 0;
  while (buffers()->find(GetBufferName(prefix, count)) != buffers()->end()) {
    count++;
  }
  return GetBufferName(prefix, count);
}

void EditorState::set_exit_value(int exit_value) { exit_value_ = exit_value; }

void EditorState::Terminate(TerminationType termination_type, int exit_value) {
  status().SetInformationText(L"Exit: Preparing to close buffers (" +
                              std::to_wstring(buffers_.size()) + L")");
  if (termination_type == TerminationType::kWhenClean) {
    LOG(INFO) << "Checking buffers for termination.";
    std::vector<wstring> buffers_with_problems;
    for (auto& it : buffers_) {
      if (auto result = it.second.ptr()->IsUnableToPrepareToClose();
          result.IsError()) {
        buffers_with_problems.push_back(
            it.second.ptr()->Read(buffer_variables::name));
        it.second.ptr()->status().SetWarningText(
            Error::Augment(L"Unable to close", result.error()).description);
      }
    }
    if (!buffers_with_problems.empty()) {
      wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
      for (auto name : buffers_with_problems) {
        error += L" " + name;
      }
      status_.SetWarningText(error);
      return;
    }
  }

  std::shared_ptr<std::set<gc::Root<OpenBuffer>>> pending_buffers(
      new std::set<gc::Root<OpenBuffer>>(),
      [this, exit_value,
       termination_type](std::set<gc::Root<OpenBuffer>>* value) {
        if (!value->empty()) {
          LOG(INFO) << "Termination attempt didn't complete successfully. It "
                       "must mean that a new one has started.";
          delete value;
          return;
        }
        delete value;
        // Since `PrepareToClose is asynchronous, we must check that they are
        // all ready to be deleted.
        if (termination_type == TerminationType::kIgnoringErrors) {
          exit_value_ = exit_value;
          return;
        }
        LOG(INFO) << "Checking buffers state for termination.";
        std::vector<wstring> buffers_with_problems;
        for (auto& it : buffers_) {
          if (it.second.ptr()->dirty() &&
              !it.second.ptr()->Read(buffer_variables::allow_dirty_delete)) {
            buffers_with_problems.push_back(
                it.second.ptr()->Read(buffer_variables::name));
          }
        }
        if (!buffers_with_problems.empty()) {
          wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
          for (auto name : buffers_with_problems) {
            error += L" " + name;
          }
          return status_.SetWarningText(error);
        }
        LOG(INFO) << "Terminating.";
        status().SetInformationText(
            L"Exit: All buffers closed, shutting down.");
        exit_value_ = exit_value;
      });

  auto decrement = [this, pending_buffers](const gc::Root<OpenBuffer>& buffer,
                                           PossibleError) {
    pending_buffers->erase(buffer);
    std::wstring extra;
    std::wstring separator = L": ";
    int count = 0;
    for (auto& buffer : *pending_buffers) {
      if (count < 5) {
        extra += separator + buffer.ptr()->name().read();
        separator = L", ";
      } else if (count == 5) {
        extra += L"…";
      }
      count++;
    }
    status().SetInformationText(L"Exit: Closing buffers: Remaining: " +
                                std::to_wstring(pending_buffers->size()) +
                                extra);
  };

  for (const auto& it : buffers_) {
    pending_buffers->insert(it.second);
    it.second.ptr()->PrepareToClose().SetConsumer(
        std::bind_front(decrement, it.second));
  }
}

futures::Value<EmptyValue> EditorState::ProcessInputString(
    const string& input) {
  return futures::ForEachWithCopy(
             input.begin(), input.end(),
             [this](int c) {
               return ProcessInput(c).Transform([](EmptyValue) {
                 return futures::IterationControlCommand::kContinue;
               });
             })
      .Transform([](futures::IterationControlCommand) { return EmptyValue(); });
}

futures::Value<EmptyValue> EditorState::ProcessInput(int c) {
  if (auto handler = keyboard_redirect().get(); handler != nullptr) {
    handler->ProcessInput(c);
    return futures::Past(EmptyValue());
  }

  if (has_current_buffer()) {
    current_buffer()->ptr()->mode().ProcessInput(c);
    return futures::Past(EmptyValue());
  }

  return OpenAnonymousBuffer(*this).Transform([this,
                                               c](gc::Root<OpenBuffer> buffer) {
    if (!has_current_buffer()) {
      buffer_tree_.AddBuffer(buffer, BuffersList::AddBufferType::kOnlyList);
      set_current_buffer(buffer, CommandArgumentModeApplyMode::kFinal);
      CHECK(has_current_buffer());
      CHECK(&current_buffer().value().ptr().value() == &buffer.ptr().value());
    }
    buffer.ptr()->mode().ProcessInput(c);
    return EmptyValue();
  });
}

void EditorState::MoveBufferForwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer.has_value()) {
    it = buffers_.find(buffer->ptr()->name());
  }

  if (it == buffers_.end()) {
    if (buffers_.empty()) {
      return;
    }
    it = buffers_.begin();
  }

  times = times % buffers_.size();
  for (size_t repetition = 0; repetition < times; repetition++) {
    ++it;
    if (it == buffers_.end()) {
      it = buffers_.begin();
    }
  }
  set_current_buffer(it->second, CommandArgumentModeApplyMode::kFinal);
  PushCurrentPosition();
}

void EditorState::MoveBufferBackwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer.has_value()) {
    it = buffers_.find(buffer->ptr()->name());
  }

  if (it == buffers_.end()) {
    if (buffers_.empty()) {
      return;
    }
    --it;
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    if (it == buffers_.begin()) {
      it = buffers_.end();
    }
    --it;
  }
  set_current_buffer(it->second, CommandArgumentModeApplyMode::kFinal);
  PushCurrentPosition();
}

std::optional<EditorState::ScreenState> EditorState::FlushScreenState() {
  auto now = Now();
  if (now < next_screen_update_) {
    // This is enough to cause the main loop to wake up; it'll attempt to do a
    // redraw then. Multiple attempts may be scheduled, but that's fine (just
    // a bit wasteful of memory).
    work_queue_->ScheduleAt(next_screen_update_, [] {});
    return {};
  }
  next_screen_update_ = AddSeconds(now, 1.0 / frames_per_second_);
  ScreenState output = screen_state_;
  screen_state_ = ScreenState();
  return output;
}

// We will store the positions in a special buffer.  They will be sorted from
// old (top) to new (bottom), one per line.  Each line will be of the form:
//
//   line column buffer
//
// The current line position is set to one line after the line to be returned by
// a pop.  To insert a new position, we insert it right at the current line.
static const BufferName& PositionsBufferName() {
  static const BufferName* const output = new BufferName(L"- positions");
  return *output;
}

void EditorState::PushCurrentPosition() {
  auto buffer = current_buffer();
  if (buffer.has_value()) {
    PushPosition(buffer->ptr()->position());
  }
}

void EditorState::PushPosition(LineColumn position) {
  auto buffer = current_buffer();
  if (!buffer.has_value() ||
      !buffer->ptr()->Read(buffer_variables::push_positions_to_history)) {
    return;
  }
  auto buffer_it = buffers_.find(PositionsBufferName());
  futures::Value<gc::Root<OpenBuffer>> positions_buffer =
      buffer_it != buffers_.end()
          ? futures::Past(buffer_it->second)
          // Insert a new entry into the list of buffers.
          : OpenOrCreateFile(
                OpenFileOptions{
                    .editor_state = *this,
                    .name = PositionsBufferName(),
                    .path = edge_path().empty()
                                ? std::optional<Path>()
                                : Path::Join(
                                      edge_path().front(),
                                      Path::FromString(L"positions").value()),
                    .insertion_type = BuffersList::AddBufferType::kIgnore})
                .Transform([](gc::Root<OpenBuffer> buffer_root) {
                  OpenBuffer& buffer = buffer_root.ptr().value();
                  buffer.Set(buffer_variables::save_on_close, true);
                  buffer.Set(buffer_variables::trigger_reload_on_buffer_write,
                             false);
                  buffer.Set(buffer_variables::show_in_buffers_list, false);
                  return buffer_root;
                });

  positions_buffer.SetConsumer(
      [line_to_insert = MakeNonNullShared<Line>(
           position.ToString() + L" " +
           buffer->ptr()->Read(buffer_variables::name))](
          gc::Root<OpenBuffer> buffer) {
        buffer.ptr()->CheckPosition();
        CHECK_LE(buffer.ptr()->position().line,
                 LineNumber(0) + buffer.ptr()->contents().size());
        buffer.ptr()->InsertLine(buffer.ptr()->current_position_line(),
                                 line_to_insert);
        CHECK_LE(buffer.ptr()->position().line,
                 LineNumber(0) + buffer.ptr()->contents().size());
      });
}

static BufferPosition PositionFromLine(const wstring& line) {
  std::wstringstream line_stream(line);
  LineColumn position;
  line_stream >> position.line.line >> position.column.column;
  line_stream.get();
  std::wstring buffer_name;
  getline(line_stream, buffer_name);
  return BufferPosition{.buffer_name = BufferName(buffer_name),
                        .position = std::move(position)};
}

gc::Root<OpenBuffer> EditorState::GetConsole() {
  auto name = BufferName(L"- console");
  return FindOrBuildBuffer(name, [&] {
    gc::Root<OpenBuffer> buffer_root =
        OpenBuffer::New({.editor = *this, .name = name});
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.Set(buffer_variables::allow_dirty_delete, true);
    buffer.Set(buffer_variables::show_in_buffers_list, false);
    buffer.Set(buffer_variables::persist_state, false);
    return buffer_root;
  });
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(PositionsBufferName());
  return it != buffers_.end() &&
         it->second.ptr()->contents().size() > LineNumberDelta(1);
}

BufferPosition EditorState::ReadPositionsStack() {
  CHECK(HasPositionsInStack());
  gc::Ptr<OpenBuffer> buffer =
      buffers_.find(PositionsBufferName())->second.ptr();
  return PositionFromLine(buffer->current_line()->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: Direction::kForwards
  // means the user is actually going "back" in the history, which means we have
  // to decrement the line counter.
  CHECK(HasPositionsInStack());
  gc::Ptr<OpenBuffer> buffer =
      buffers_.find(PositionsBufferName())->second.ptr();
  if (direction == Direction::kBackwards) {
    if (buffer->current_position_line() >= buffer->EndLine()) {
      return false;
    }
    buffer->set_current_position_line(buffer->current_position_line() +
                                      LineNumberDelta(1));
    return true;
  }

  if (buffer->current_position_line() == LineNumber(0)) {
    return false;
  }
  buffer->set_current_position_line(buffer->current_position_line() -
                                    LineNumberDelta(1));
  return true;
}

Status& EditorState::status() { return status_; }
const Status& EditorState::status() const { return status_; }

Path EditorState::expand_path(Path path) const {
  return Path::ExpandHomeDirectory(home_directory(), path);
}

void EditorState::PushSignal(UnixSignal signal) {
  pending_signals_.push_back(signal);
}

void EditorState::ProcessSignals() {
  if (pending_signals_.empty()) {
    return;
  }
  vector<UnixSignal> signals;
  signals.swap(pending_signals_);
  for (UnixSignal signal : signals) {
    switch (signal.read()) {
      case SIGINT:
      case SIGTSTP:
        ForEachActiveBuffer([signal](OpenBuffer& buffer) {
          buffer.PushSignal(signal);
          return futures::Past(EmptyValue());
        });
    }
  }
}

bool EditorState::handling_stop_signals() const {
  for (auto& buffer : active_buffers())
    if (buffer.ptr()->Read(buffer_variables::pts)) return true;
  return false;
}

}  // namespace afc::editor
