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

#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor_vm.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/audio.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/server.h"
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
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::NonNull;
using language::Observers;
using language::overload;
using language::Pointer;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;

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

ThreadPool& EditorState::thread_pool() { return thread_pool_.value(); }

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

void ReclaimAndSchedule(gc::Pool& pool, WorkQueue& work_queue) {
  gc::Pool::CollectOutput collect_output = pool.Collect();
  static constexpr size_t kSecondsBetweenGc = 1;
  work_queue.Schedule(WorkQueue::Callback{
      .time = AddSeconds(Now(), std::get_if<gc::Pool::UnfinishedCollectStats>(
                                    &collect_output) == nullptr
                                    ? kSecondsBetweenGc
                                    : 0.0),
      .callback = [&pool, &work_queue] {
        ReclaimAndSchedule(pool, work_queue);
      }});
}

EditorState::EditorState(CommandLineValues args,
                         infrastructure::audio::Player& audio_player)
    : work_queue_(WorkQueue::New()),
      thread_pool_(MakeNonNullShared<ThreadPool>(32, work_queue_.get_shared())),
      gc_pool_({.collect_duration_threshold = 0.05,
                .thread_pool = thread_pool_.get_shared()}),
      string_variables_(editor_variables::StringStruct()->NewInstance()),
      bool_variables_(editor_variables::BoolStruct()->NewInstance()),
      int_variables_(editor_variables::IntStruct()->NewInstance()),
      double_variables_(editor_variables::DoubleStruct()->NewInstance()),
      home_directory_(args.home_directory),
      edge_path_([](std::vector<std::wstring> paths) {
        std::vector<Path> output;
        for (std::wstring& candidate : paths) {
          std::visit(overload{IgnoreErrors{},
                              [&](Path path) { output.push_back(path); }},
                     Path::FromString(candidate));
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
    auto path =
        Path::Join(dir, ValueOrDie(Path::FromString(L"hooks/start.cc")));
    return std::visit(
        overload{
            [&](const NonNull<std::unique_ptr<vm::Expression>>& expression)
                -> futures::Value<futures::IterationControlCommand> {
              LOG(INFO) << "Evaluating file: " << path;
              return Evaluate(expression.value(), gc_pool_, environment_,
                              [path, work_queue = work_queue()](
                                  std::function<void()> resume) {
                                LOG(INFO)
                                    << "Evaluation of file yields: " << path;
                                work_queue->Schedule(WorkQueue::Callback{
                                    .callback = std::move(resume)});
                              })
                  .Transform([](gc::Root<vm::Value>) {
                    // TODO(2022-04-26): Figure out a way to get rid of
                    // `Success`.
                    return futures::Past(
                        Success(futures::IterationControlCommand::kContinue));
                  })
                  .ConsumeErrors([](Error) {
                    return futures::Past(
                        futures::IterationControlCommand::kContinue);
                  });
            },
            [&](Error error) {
              LOG(INFO) << "Compilation error: " << error;
              status_.Set(error);
              return futures::Past(futures::IterationControlCommand::kContinue);
            }},
        CompileFile(path, gc_pool_, environment_));
  });

  double_variables_.ObserveValue(editor_variables::volume).Add([this] {
    audio_player_.SetVolume(infrastructure::audio::Volume(
        std::max(0.0, std::min(1.0, Read(editor_variables::volume)))));
    return Observers::State::kAlive;
  });

  ReclaimAndSchedule(gc_pool_, work_queue_.value());
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

  LOG(INFO) << "Reclaim GC pool.";
  gc_pool_.FullCollect();
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

const std::wstring& EditorState::Read(
    const EdgeVariable<std::wstring>* variable) const {
  return string_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<std::wstring>* variable,
                      std::wstring value) {
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
  VisitPointer(
      buffer_tree_.active_buffer(),
      [](gc::Root<OpenBuffer> buffer) { buffer.ptr()->CheckPosition(); },
      [] {});
}

void EditorState::CloseBuffer(OpenBuffer& buffer) {
  buffer.PrepareToClose().SetConsumer(VisitCallback(
      overload{[this, buffer = buffer.NewRoot()](Error error) {
                 buffer.ptr()->status().Set(AugmentError(
                     L"🖝  Unable to close (“*ad” to ignore): " +
                         buffer.ptr()->Read(buffer_variables::name),
                     error));
               },
               [this, buffer = buffer.NewRoot()](EmptyValue) {
                 buffer.ptr()->Close();
                 buffer_tree_.RemoveBuffer(buffer.ptr().value());
                 buffers_.erase(buffer.ptr()->name());
                 AdjustWidgets();
               }}));
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
        (std::max(modifiers().repetitions.value(), 1ul) - 1) %
        buffer_tree().BuffersCount());
    value = callback(buffer.ptr().value());
  }
  return std::move(value).Transform([this](EmptyValue) {
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

BufferName GetBufferName(const std::wstring& prefix, size_t count) {
  return BufferName(prefix + L" " + std::to_wstring(count));
}

BufferName EditorState::GetUnusedBufferName(const std::wstring& prefix) {
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
    std::vector<std::wstring> buffers_with_problems;
    for (auto& it : buffers_)
      std::visit(overload{[](EmptyValue) {},
                          [&](Error error) {
                            buffers_with_problems.push_back(
                                it.second.ptr()->Read(buffer_variables::name));
                            it.second.ptr()->status().Set(
                                AugmentError(L"Unable to close", error));
                          }},
                 it.second.ptr()->IsUnableToPrepareToClose());

    if (!buffers_with_problems.empty()) {
      std::wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
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
        std::vector<std::wstring> buffers_with_problems;
        for (auto& it : buffers_) {
          if (it.second.ptr()->dirty() &&
              !it.second.ptr()->Read(buffer_variables::allow_dirty_delete)) {
            buffers_with_problems.push_back(
                it.second.ptr()->Read(buffer_variables::name));
          }
        }
        if (!buffers_with_problems.empty()) {
          std::wstring error = L"🖝  Dirty buffers (“*aq” to ignore):";
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

  auto decrement = [this, pending_buffers](
                       const gc::Root<OpenBuffer>& buffer_done, PossibleError) {
    pending_buffers->erase(buffer_done);
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

void EditorState::ResetModifiers() {
  auto buffer = current_buffer();
  if (buffer.has_value()) {
    buffer->ptr()->ResetMode();
  }
  modifiers_.ResetSoft();
}

Direction EditorState::direction() const { return modifiers_.direction; }

void EditorState::set_direction(Direction direction) {
  modifiers_.direction = direction;
}

void EditorState::ResetDirection() { modifiers_.ResetDirection(); }

Direction EditorState::default_direction() const {
  return modifiers_.default_direction;
}

void EditorState::set_default_direction(Direction direction) {
  modifiers_.default_direction = direction;
  ResetDirection();
}

std::optional<size_t> EditorState::repetitions() const {
  return modifiers_.repetitions;
}

void EditorState::ResetRepetitions() { modifiers_.ResetRepetitions(); }

void EditorState::set_repetitions(size_t value) {
  modifiers_.repetitions = value;
}

Modifiers EditorState::modifiers() const { return modifiers_; }

void EditorState::set_modifiers(const Modifiers& modifiers) {
  modifiers_ = modifiers;
}

Structure EditorState::structure() const { return modifiers_.structure; }

void EditorState::set_structure(Structure structure) {
  modifiers_.structure = structure;
}

void EditorState::ResetStructure() { modifiers_.ResetStructure(); }

bool EditorState::sticky_structure() const {
  return modifiers_.sticky_structure;
}

void EditorState::set_sticky_structure(bool sticky_structure) {
  modifiers_.sticky_structure = sticky_structure;
}

Modifiers::ModifyMode EditorState::insertion_modifier() const {
  return modifiers_.insertion;
}

void EditorState::set_insertion_modifier(
    Modifiers::ModifyMode insertion_modifier) {
  modifiers_.insertion = insertion_modifier;
}

void EditorState::ResetInsertionModifier() { modifiers_.ResetInsertion(); }

Modifiers::ModifyMode EditorState::default_insertion_modifier() const {
  return modifiers_.default_insertion;
}

void EditorState::set_default_insertion_modifier(
    Modifiers::ModifyMode default_insertion_modifier) {
  modifiers_.default_insertion = default_insertion_modifier;
}

futures::Value<EmptyValue> EditorState::ProcessInputString(
    const std::string& input) {
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
    work_queue_->Schedule(
        WorkQueue::Callback{.time = next_screen_update_, .callback = [] {}});
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
  futures::Value<gc::Root<OpenBuffer>> future_positions_buffer =
      buffer_it != buffers_.end()
          ? futures::Past(buffer_it->second)
          // Insert a new entry into the list of buffers.
          : OpenOrCreateFile(
                OpenFileOptions{
                    .editor_state = *this,
                    .name = PositionsBufferName(),
                    .path = edge_path().empty()
                                ? std::optional<Path>()
                                : Path::Join(edge_path().front(),
                                             ValueOrDie(Path::FromString(
                                                 L"positions"))),
                    .insertion_type = BuffersList::AddBufferType::kIgnore})
                .Transform([](gc::Root<OpenBuffer> buffer_root) {
                  OpenBuffer& output = buffer_root.ptr().value();
                  output.Set(buffer_variables::save_on_close, true);
                  output.Set(buffer_variables::trigger_reload_on_buffer_write,
                             false);
                  output.Set(buffer_variables::show_in_buffers_list, false);
                  output.Set(buffer_variables::vm_lines_evaluation, false);
                  return buffer_root;
                });

  std::move(future_positions_buffer)
      .SetConsumer([line_to_insert = MakeNonNullShared<Line>(
                        position.ToString() + L" " +
                        buffer->ptr()->Read(buffer_variables::name))](
                       gc::Root<OpenBuffer> positions_buffer_root) {
        OpenBuffer& positions_buffer = positions_buffer_root.ptr().value();
        positions_buffer.CheckPosition();
        CHECK_LE(positions_buffer.position().line,
                 LineNumber(0) + positions_buffer.contents().size());
        positions_buffer.InsertLine(positions_buffer.current_position_line(),
                                    line_to_insert);
        CHECK_LE(positions_buffer.position().line,
                 LineNumber(0) + positions_buffer.contents().size());
      });
}

static BufferPosition PositionFromLine(const std::wstring& line) {
  std::wstringstream line_stream(line);
  LineColumn position;
  size_t line_number;
  size_t column_number;
  line_stream >> line_number >> column_number;
  // TODO(easy, 2022-06-06): Define operator>> and use it here?
  position.line = LineNumber(line_number);
  position.column = ColumnNumber(column_number);
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
  std::vector<UnixSignal> signals;
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
