#include "src/buffer.h"

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

extern "C" {
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_contents_util.h"
#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/char_buffer.h"
#include "src/command_with_modifiers.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/observers_gc.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/lazy_string_functional.h"
#include "src/line_column_vm.h"
#include "src/line_marks.h"
#include "src/map_mode.h"
#include "src/run_command_handler.h"
#include "src/screen.h"
#include "src/screen_vm.h"
#include "src/seek.h"
#include "src/server.h"
#include "src/status.h"
#include "src/substring.h"
#include "src/tokenize.h"
#include "src/transformation.h"
#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/stack.h"
#include "src/url.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"
#include "src/vm_transformation.h"

namespace afc::editor {
using concurrent::WorkQueue;
using futures::IterationControlCommand;
using infrastructure::AbsolutePath;
using infrastructure::AddSeconds;
using infrastructure::FileDescriptor;
using infrastructure::FileSystemDriver;
using infrastructure::Now;
using infrastructure::Path;
using infrastructure::PathComponent;
using infrastructure::Tracker;
using infrastructure::UpdateIfMillisecondsHavePassed;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::ObservableValue;
using language::Observers;
using language::Pointer;
using language::PossibleError;
using language::ShellEscape;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::VisitPointer;
using language::WeakPtrLockingObserver;

namespace gc = language::gc;

namespace {
static const wchar_t* kOldCursors = L"old-cursors";

NonNull<std::shared_ptr<const Line>> AddLineMetadata(
    OpenBuffer& buffer, NonNull<std::shared_ptr<const Line>> line) {
  if (line->metadata() != nullptr) return line;
  if (line->empty()) return line;

  auto compilation_result = buffer.CompileString(line->contents()->ToString());
  if (compilation_result.IsError()) return line;
  auto [expr, sub_environment] = std::move(compilation_result.value());
  std::wstring description = L"C++: " + TypesToString(expr->Types());
  if (expr->purity() == Expression::PurityType::kPure) {
    description += L" ...";
  }

  futures::ListenableValue<NonNull<std::shared_ptr<LazyString>>> metadata_value(
      futures::Future<NonNull<std::shared_ptr<LazyString>>>().value);

  if (expr->purity() == Expression::PurityType::kPure) {
    if (expr->Types() == std::vector<VMType>({VMType::Void()})) {
      return MakeNonNullShared<const Line>(
          line->CopyOptions().SetMetadata(std::nullopt));
    }
    futures::Future<NonNull<std::shared_ptr<LazyString>>> metadata_future;
    buffer.work_queue()->Schedule(
        [buffer = buffer.NewRoot(),
         expr = NonNull<std::shared_ptr<Expression>>(std::move(expr)),
         sub_environment, consumer = metadata_future.consumer] {
          buffer.ptr()
              ->EvaluateExpression(expr.value(), sub_environment)
              .Transform([](gc::Root<Value> value) {
                std::ostringstream oss;
                oss << value.ptr().value();
                // TODO(2022-04-26): Improve futures to be able to remove
                // Success.
                return Success(NewLazyString(FromByteString(oss.str())));
              })
              .ConsumeErrors([](Error error) {
                return futures::Past(
                    NewLazyString(L"E: " + std::move(error.description)));
              })
              .Transform(
                  [consumer](NonNull<std::shared_ptr<LazyString>> output) {
                    consumer(output);
                    return Success();
                  });
        });
    metadata_value = std::move(metadata_future.value);
  }

  return MakeNonNullShared<const Line>(line->CopyOptions().SetMetadata(
      Line::MetadataEntry{.initial_value = NewLazyString(description),
                          .value = std::move(metadata_value)}));
}

// We receive `contents` explicitly since `buffer` only gives us const access.
void AddLineMetadata(OpenBuffer& buffer, BufferContents& contents,
                     LineNumber position) {
  contents.set_line(position,
                    AddLineMetadata(buffer, buffer.contents().at(position)));
}

// next_scheduled_execution holds the smallest time at which we know we have
// scheduled an execution of work_queue_ in the editor's work queue.
Observers::State MaybeScheduleNextWorkQueueExecution(
    std::weak_ptr<WorkQueue> work_queue_weak,
    NonNull<std::shared_ptr<WorkQueue>> parent_work_queue,
    NonNull<std::shared_ptr<std::optional<struct timespec>>>
        next_scheduled_execution) {
  auto work_queue = work_queue_weak.lock();
  if (work_queue == nullptr) return Observers::State::kExpired;
  if (auto next = work_queue->NextExecution();
      next.has_value() && next != next_scheduled_execution.value()) {
    next_scheduled_execution.value() = next;
    parent_work_queue->ScheduleAt(
        next.value(),
        [work_queue, parent_work_queue, next_scheduled_execution]() mutable {
          next_scheduled_execution.value() = std::nullopt;
          work_queue->Execute();
          MaybeScheduleNextWorkQueueExecution(work_queue, parent_work_queue,
                                              next_scheduled_execution);
        });
  }
  return Observers::State::kAlive;
}
}  // namespace

using namespace afc::vm;
using std::to_wstring;

// Name of the buffer that holds the contents that have been deleted recently
// and which should still be included in the delete buffer for additional
// deletions.
//
// This is used so that multiple subsequent deletion transformations (without
// any interspersed non-delete transformations) will all aggregate into the
// paste buffer (rather than retaining only the deletion corresponding to the
// last such transformation).
/* static */ const BufferName kFuturePasteBuffer =
    BufferName(L"- future paste buffer");

/* static */ gc::Root<OpenBuffer> OpenBuffer::New(Options options) {
  gc::Root<OpenBuffer> output =
      options.editor.gc_pool().NewRoot(MakeNonNullUnique<OpenBuffer>(
          ConstructorAccessTag(), std::move(options)));
  output.ptr()->Initialize(output.ptr());
  return output;
}

OpenBuffer::OpenBuffer(ConstructorAccessTag, Options options)
    : options_(std::move(options)),
      work_queue_(WorkQueue::New()),
      bool_variables_(buffer_variables::BoolStruct()->NewInstance()),
      string_variables_(buffer_variables::StringStruct()->NewInstance()),
      int_variables_(buffer_variables::IntStruct()->NewInstance()),
      double_variables_(buffer_variables::DoubleStruct()->NewInstance()),
      line_column_variables_(
          buffer_variables::LineColumnStruct()->NewInstance()),
      environment_(editor()
                       .gc_pool()
                       .NewRoot(MakeNonNullUnique<Environment>(
                           options_.editor.environment().ptr()))
                       .ptr()),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()),
      default_commands_(options_.editor.default_commands()->NewChild()),
      mode_(MakeNonNullUnique<MapMode>(default_commands_)),
      status_(options_.editor.audio_player()),
      file_system_driver_(editor().thread_pool()) {
  work_queue_->OnSchedule().Add(std::bind_front(
      MaybeScheduleNextWorkQueueExecution,
      std::weak_ptr<WorkQueue>(work_queue_.get_shared()), editor().work_queue(),
      NonNull<std::shared_ptr<std::optional<struct timespec>>>()));
  for (auto* v :
       {buffer_variables::symbol_characters, buffer_variables::tree_parser,
        buffer_variables::language_keywords, buffer_variables::typos,
        buffer_variables::identifier_behavior})
    string_variables_.ObserveValue(v).Add([this] {
      UpdateTreeParser();
      MaybeStartUpdatingSyntaxTrees();
      return Observers::State::kAlive;
    });
}

OpenBuffer::~OpenBuffer() { LOG(INFO) << "Start destructor: " << name(); }

EditorState& OpenBuffer::editor() const { return options_.editor; }

Status& OpenBuffer::status() const { return status_; }

PossibleError OpenBuffer::IsUnableToPrepareToClose() const {
  if (options_.editor.modifiers().strength > Modifiers::Strength::kNormal) {
    return Success();
  }
  if (child_pid_ != -1) {
    if (!Read(buffer_variables::term_on_close)) {
      return Error(L"Running subprocess (pid: " + std::to_wstring(child_pid_) +
                   L")");
    }
    return Success();
  }
  if (dirty() && !Read(buffer_variables::save_on_close) &&
      !Read(buffer_variables::allow_dirty_delete)) {
    return Error(L"Unsaved changes");
  }
  return Success();
}

futures::Value<PossibleError> OpenBuffer::PrepareToClose() {
  log_->Append(L"PrepareToClose");
  LOG(INFO) << "Preparing to close: " << Read(buffer_variables::name);
  if (auto is_unable = IsUnableToPrepareToClose(); is_unable.IsError()) {
    LOG(INFO) << name() << ": Unable to close: " << is_unable.error();
    return futures::Past(is_unable);
  }

  return (options_.editor.modifiers().strength == Modifiers::Strength::kNormal
              ? PersistState()
              : futures::IgnoreErrors(PersistState()))
      .Transform([this](EmptyValue) {
        LOG(INFO) << name() << ": State persisted.";
        if (child_pid_ != -1) {
          if (Read(buffer_variables::term_on_close)) {
            LOG(INFO) << "Sending termination and preparing handler: "
                      << Read(buffer_variables::name);
            kill(child_pid_, SIGTERM);
            auto future = futures::Future<PossibleError>();
            on_exit_handler_ =
                [this, consumer = std::move(future.consumer)]() mutable {
                  CHECK_EQ(child_pid_, -1);
                  LOG(INFO) << "Subprocess terminated: "
                            << Read(buffer_variables::name);
                  PrepareToClose().SetConsumer(std::move(consumer));
                };
            return std::move(future.value);
          }
          CHECK(options_.editor.modifiers().strength >
                Modifiers::Strength::kNormal);
        }
        if (!dirty() || !Read(buffer_variables::save_on_close)) {
          return futures::Past(ValueOrError(Success()));
        }
        LOG(INFO) << name() << ": attempting to save buffer.";
        return Save();
      });
}

void OpenBuffer::Close() {
  log_->Append(L"Closing");
  if (dirty() && Read(buffer_variables::save_on_close)) {
    log_->Append(L"Saving buffer: " + Read(buffer_variables::name));
    Save();
  }
  editor().line_marks().RemoveSource(name());
  close_observers_.Notify();
}

futures::Value<EmptyValue> OpenBuffer::WaitForEndOfFile() {
  if (fd_ == nullptr && fd_error_ == nullptr &&
      reload_state_ == ReloadState::kDone) {
    return futures::Past(EmptyValue());
  }
  return end_of_file_observers_.NewFuture();
}

futures::Value<EmptyValue> OpenBuffer::NewCloseFuture() {
  return close_observers_.NewFuture();
}

void OpenBuffer::Enter() {
  if (Read(buffer_variables::reload_on_enter)) {
    Reload();
    CheckPosition();
  }
}

void OpenBuffer::Visit() {
  Enter();
  UpdateLastAction();
  last_visit_ = last_action_;
  if (options_.handle_visit != nullptr) {
    options_.handle_visit(*this);
  }
}

struct timespec OpenBuffer::last_visit() const { return last_visit_; }
struct timespec OpenBuffer::last_action() const { return last_action_; }

futures::Value<PossibleError> OpenBuffer::PersistState() const {
  auto trace = log_->NewChild(L"Persist State");
  if (!Read(buffer_variables::persist_state)) {
    return futures::Past(ValueOrError(Success()));
  }

  return OnError(
             GetEdgeStateDirectory(),
             [this](Error error) {
               status().SetWarningText(L"Unable to get Edge state directory: " +
                                       error.description);
               return futures::Past(error);
             })
      .Transform([this,
                  root_this = ptr_this_->ToRoot()](Path edge_state_directory) {
        auto path =
            Path::Join(edge_state_directory,
                       PathComponent::FromString(L".edge_state").value());
        LOG(INFO) << "PersistState: Preparing state file: " << path;
        NonNull<std::unique_ptr<BufferContents>> contents;
        contents->push_back(L"// State of file: " + path.read());
        contents->push_back(L"");

        contents->push_back(L"buffer.set_position(" + position().ToCppString() +
                            L");");
        contents->push_back(L"");

        contents->push_back(L"// String variables");
        for (const auto& variable :
             buffer_variables::StringStruct()->variables()) {
          contents->push_back(L"buffer.set_" + variable.first + L"(\"" +
                              CppEscapeString(Read(variable.second.get())) +
                              L"\");");
        }
        contents->push_back(L"");

        contents->push_back(L"// Int variables");
        for (const auto& variable :
             buffer_variables::IntStruct()->variables()) {
          contents->push_back(L"buffer.set_" + variable.first + L"(" +
                              std::to_wstring(Read(variable.second.get())) +
                              L");");
        }
        contents->push_back(L"");

        contents->push_back(L"// Bool variables");
        for (const auto& variable :
             buffer_variables::BoolStruct()->variables()) {
          contents->push_back(
              L"buffer.set_" + variable.first + L"(" +
              (Read(variable.second.get()) ? L"true" : L"false") + L");");
        }
        contents->push_back(L"");

        contents->push_back(L"// LineColumn variables");
        for (const auto& variable :
             buffer_variables::LineColumnStruct()->variables()) {
          contents->push_back(L"buffer.set_" + variable.first + L"(" +
                              Read(variable.second.get()).ToCppString() +
                              L");");
        }
        contents->push_back(L"");

        return futures::OnError(
            SaveContentsToFile(path, std::move(contents),
                               editor().thread_pool(), file_system_driver()),
            [root_this](Error error) {
              root_this.ptr()->status().SetWarningText(
                  L"Unable to persist state: " + error.description);
              return futures::Past(error);
            });
      });
}

void OpenBuffer::ClearContents(
    BufferContents::CursorsBehavior cursors_behavior) {
  VLOG(5) << "Clear contents of buffer: " << Read(buffer_variables::name);
  options_.editor.line_marks().RemoveExpiredMarksFromSource(name());
  options_.editor.line_marks().ExpireMarksFromSource(contents(), name());
  contents_.EraseLines(LineNumber(0), LineNumber(0) + contents_.size(),
                       cursors_behavior);
  if (terminal_ != nullptr) {
    terminal_->SetPosition(LineColumn());
  }
  undo_past_.clear();
  undo_future_.clear();
}

void OpenBuffer::AppendEmptyLine() {
  auto follower = GetEndPositionFollower();
  contents_.push_back(NonNull<std::shared_ptr<Line>>());
}

void OpenBuffer::EndOfFile() {
  UpdateLastAction();
  CHECK(fd_ == nullptr);
  CHECK(fd_error_ == nullptr);
  if (child_pid_ != -1) {
    int exit_status;
    if (waitpid(child_pid_, &exit_status, 0) == -1) {
      status_.SetWarningText(L"waitpid failed: " +
                             FromByteString(strerror(errno)));
      return;
    }
    child_exit_status_ = exit_status;
    clock_gettime(0, &time_last_exit_);

    child_pid_ = -1;
    if (on_exit_handler_) {
      on_exit_handler_();
      on_exit_handler_ = nullptr;
    }
  }

  // We can remove expired marks now. We know that the set of fresh marks is now
  // complete.
  editor().line_marks().RemoveExpiredMarksFromSource(name());

  end_of_file_observers_.Notify();

  if (Read(buffer_variables::reload_after_exit)) {
    Set(buffer_variables::reload_after_exit,
        Read(buffer_variables::default_reload_after_exit));
    Reload();
  }
  if (Read(buffer_variables::close_after_clean_exit) &&
      child_exit_status_.has_value() && WIFEXITED(child_exit_status_.value()) &&
      WEXITSTATUS(child_exit_status_.value()) == 0) {
    editor().CloseBuffer(*this);
  }

  std::optional<gc::Root<OpenBuffer>> current_buffer =
      editor().current_buffer();
  if (current_buffer.has_value() && name() == BufferName::BuffersList()) {
    current_buffer->ptr()->Reload();
  }
}

void OpenBuffer::SendEndOfFileToProcess() {
  if (fd() == nullptr) {
    status().SetInformationText(L"No active subprocess for current buffer.");
    return;
  }
  if (Read(buffer_variables::pts)) {
    char str[1] = {4};
    if (write(fd()->fd().read(), str, sizeof(str)) == -1) {
      status().SetInformationText(L"Sending EOF failed: " +
                                  FromByteString(strerror(errno)));
      return;
    }
    status().SetInformationText(L"EOF sent");
  } else {
    if (shutdown(fd()->fd().read(), SHUT_WR) == -1) {
      status().SetInformationText(L"shutdown(SHUT_WR) failed: " +
                                  FromByteString(strerror(errno)));
      return;
    }
    status().SetInformationText(L"shutdown sent");
  }
}

std::unique_ptr<bool, std::function<void(bool*)>>
OpenBuffer::GetEndPositionFollower() {
  if (!Read(buffer_variables::follow_end_of_file)) {
    return nullptr;
  }
  if (position() < end_position() && terminal_ == nullptr) {
    return nullptr;  // Not at the end, so user must have scrolled up.
  }
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this](bool* value) {
        delete value;
        set_position(terminal_ != nullptr ? terminal_->position()
                                          : end_position());
      });
}

bool OpenBuffer::ShouldDisplayProgress() const {
  return (fd_ != nullptr || fd_error_ != nullptr) &&
         Read(buffer_variables::display_progress);
}

void OpenBuffer::RegisterProgress() {
  if (UpdateIfMillisecondsHavePassed(&last_progress_update_, 200).has_value()) {
    Set(buffer_variables::progress, Read(buffer_variables::progress) + 1);
  }
}

void OpenBuffer::ReadData() { ReadData(fd_); }
void OpenBuffer::ReadErrorData() { ReadData(fd_error_); }

void OpenBuffer::UpdateTreeParser() {
  std::wistringstream typos_stream(Read(buffer_variables::typos));
  std::wistringstream language_keywords(
      Read(buffer_variables::language_keywords));
  buffer_syntax_parser_.UpdateParser(
      {.parser_name = Read(buffer_variables::tree_parser),
       .typos_set =
           std::unordered_set<wstring>{
               std::istream_iterator<std::wstring, wchar_t>(typos_stream),
               std::istream_iterator<std::wstring, wchar_t>()},
       .language_keywords = std::unordered_set<wstring>(
           std::istream_iterator<wstring, wchar_t>(language_keywords),
           std::istream_iterator<wstring, wchar_t>()),
       .symbol_characters = Read(buffer_variables::symbol_characters),
       .identifier_behavior =
           Read(buffer_variables::identifier_behavior) == L"color-by-hash"
               ? IdentifierBehavior::kColorByHash
               : IdentifierBehavior::kNone});
}

NonNull<std::shared_ptr<const ParseTree>> OpenBuffer::parse_tree() const {
  return buffer_syntax_parser_.tree();
}

NonNull<std::shared_ptr<const ParseTree>> OpenBuffer::simplified_parse_tree()
    const {
  return buffer_syntax_parser_.simplified_tree();
}

void OpenBuffer::Initialize(gc::Ptr<OpenBuffer> ptr_this) {
  ptr_this_ = std::move(ptr_this);
  gc::WeakPtr<OpenBuffer> weak_this = ptr_this_->ToWeakPtr();
  buffer_syntax_parser_.ObserveTrees().Add(
      WeakPtrLockingObserver(weak_this, [](OpenBuffer& buffer) {
        // Trigger a wake up alarm.
        buffer.work_queue()->Schedule([] {});
      }));

  UpdateTreeParser();

  environment_->Define(L"buffer",
                       VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
                           editor().gc_pool(), NewRoot()));

  environment_->Define(
      L"sleep",
      Value::NewFunction(
          editor().gc_pool(), PurityType::kUnknown,
          {VMType::Void(), VMType::Double()},
          [weak_this](std::vector<gc::Root<Value>> args,
                      Trampoline& trampoline) {
            CHECK_EQ(args.size(), 1ul);
            double delay_seconds = args[0].ptr()->get_double();
            return VisitPointer(
                weak_this.Lock(),
                [&](gc::Root<OpenBuffer> root_this) {
                  futures::Future<ValueOrError<EvaluationOutput>> future;
                  EvaluationOutput output = vm::EvaluationOutput::Return(
                      vm::Value::NewVoid(root_this.ptr()->editor().gc_pool()));
                  root_this.ptr()->work_queue()->ScheduleAt(
                      AddSeconds(Now(), delay_seconds),
                      [weak_this, consumer = std::move(future.consumer),
                       output] {
                        VisitPointer(
                            weak_this.Lock(),
                            [&](gc::Root<OpenBuffer>) { consumer(output); },
                            [] {});
                      });
                  return std::move(future.value);
                },
                [&] {
                  return futures::Past(Success(EvaluationOutput::Return(
                      vm::Value::NewVoid(trampoline.pool()))));
                });
          }));

  Set(buffer_variables::name, options_.name.read());
  if (options_.path.has_value()) {
    Set(buffer_variables::path, options_.path.value().read());
  }
  Set(buffer_variables::pts_path, L"");
  Set(buffer_variables::command, L"");
  Set(buffer_variables::reload_after_exit, false);
  if (name() == BufferName::PasteBuffer() || name() == kFuturePasteBuffer) {
    Set(buffer_variables::allow_dirty_delete, true);
    Set(buffer_variables::show_in_buffers_list, false);
    Set(buffer_variables::delete_into_paste_buffer, false);
  }
  ClearContents(BufferContents::CursorsBehavior::kUnmodified);

  if (auto buffer_path = Path::FromString(Read(buffer_variables::path));
      !buffer_path.IsError()) {
    for (const auto& dir : options_.editor.edge_path()) {
      auto state_path = Path::Join(
          Path::Join(dir, PathComponent::FromString(L"state").value()),
          Path::Join(buffer_path.value(),
                     PathComponent::FromString(L".edge_state").value()));
      file_system_driver_.Stat(state_path)
          .Transform([state_path, weak_this](struct stat) {
            return VisitPointer(
                weak_this.Lock(),
                [&](gc::Root<OpenBuffer> root_this) {
                  return root_this.ptr()->EvaluateFile(state_path);
                },
                [] {
                  return futures::Past(ValueOrError<gc::Root<Value>>(
                      Error(L"Buffer has been deleted.")));
                });
          });
    }
  }

  contents_.SetUpdateListener(
      [weak_this](const CursorsTracker::Transformation& transformation) {
        std::optional<gc::Root<OpenBuffer>> root_this = weak_this.Lock();
        if (!root_this.has_value()) return;
        root_this->ptr()->work_queue_->Schedule([weak_this] {
          std::optional<gc::Root<OpenBuffer>> root_this = weak_this.Lock();
          if (root_this.has_value())
            root_this->ptr()->MaybeStartUpdatingSyntaxTrees();
        });
        root_this->ptr()->SetDiskState(DiskState::kStale);
        if (root_this->ptr()->Read(buffer_variables::persist_state)) {
          switch (root_this->ptr()->backup_state_) {
            case DiskState::kCurrent: {
              root_this->ptr()->backup_state_ = DiskState::kStale;
              auto flush_backup_time = Now();
              flush_backup_time.tv_sec += 30;
              root_this->ptr()->work_queue_->ScheduleAt(
                  flush_backup_time,
                  [root_this] { root_this->ptr()->UpdateBackup(); });
            } break;

            case DiskState::kStale:
              break;  // Nothing.
          }
        }
        root_this->ptr()->UpdateLastAction();
        root_this->ptr()->cursors_tracker_.AdjustCursors(transformation);
      });
}

void OpenBuffer::MaybeStartUpdatingSyntaxTrees() {
  buffer_syntax_parser_.Parse(contents_.copy());
}

void OpenBuffer::StartNewLine(NonNull<std::shared_ptr<const Line>> line) {
  static Tracker tracker(L"OpenBuffer::StartNewLine");
  auto tracker_call = tracker.Call();
  AppendLines({std::move(line)});
}

void OpenBuffer::AppendLines(
    std::vector<NonNull<std::shared_ptr<const Line>>> lines) {
  static Tracker tracker(L"OpenBuffer::AppendLines");
  auto tracker_call = tracker.Call();

  auto lines_added = LineNumberDelta(lines.size());
  if (lines_added.IsZero()) return;

  LineNumberDelta start_new_section = contents_.size() - LineNumberDelta(1);
  for (NonNull<std::shared_ptr<const Line>>& line : lines) {
    line = AddLineMetadata(*this, std::move(line));
  }
  contents_.append_back(std::move(lines));
  if (Read(buffer_variables::contains_line_marks)) {
    static Tracker tracker(L"OpenBuffer::StartNewLine::ScanForMarks");
    auto tracker_call = tracker.Call();
    auto options = ResolvePathOptions::New(
        editor(), std::make_shared<FileSystemDriver>(editor().thread_pool()));
    auto buffer_name = name();
    for (LineNumberDelta i; i < lines_added; ++i) {
      auto source_line = LineNumber() + start_new_section + i;
      options.path = contents_.at(source_line)->ToString();
      ResolvePath(options).Transform([&editor = editor(), buffer_name,
                                      source_line](ResolvePathOutput results) {
        LineMarks::Mark mark{
            .source_buffer = buffer_name,
            .source_line = source_line,
            .target_buffer = BufferName(results.path),
            .target_line_column = results.position.value_or(LineColumn())};
        LOG(INFO) << "Found a mark: " << mark;
        editor.line_marks().AddMark(std::move(mark));
        return Success();
      });
    }
  }
}

void OpenBuffer::Reload() {
  display_data_ = MakeNonNullUnique<BufferDisplayData>();

  if (child_pid_ != -1) {
    LOG(INFO) << "Sending SIGTERM.";
    kill(-child_pid_, SIGTERM);
    Set(buffer_variables::reload_after_exit, true);
    return;
  }

  switch (reload_state_) {
    case ReloadState::kDone:
      reload_state_ = ReloadState::kOngoing;
      break;
    case ReloadState::kOngoing:
      reload_state_ = ReloadState::kPending;
      return;
    case ReloadState::kPending:
      return;
  }

  auto paths = editor().edge_path();

  futures::ForEach(
      paths.begin(), paths.end(),
      [this](Path dir) {
        return EvaluateFile(
                   Path::Join(
                       dir,
                       Path::FromString(L"hooks/buffer-reload.cc").value()))
            .Transform([](gc::Root<Value>)
                           -> futures::ValueOrError<IterationControlCommand> {
              return futures::Past(IterationControlCommand::kContinue);
            })
            .ConsumeErrors(
                [](Error) { return Past(IterationControlCommand::kContinue); });
      })
      .Transform([this](IterationControlCommand) {
        if (editor().exit_value().has_value()) return futures::Past(Success());
        SetDiskState(DiskState::kCurrent);
        LOG(INFO) << "Starting reload: " << Read(buffer_variables::name);
        return options_.generate_contents != nullptr
                   ? IgnoreErrors(options_.generate_contents(*this))
                   : futures::Past(Success());
      })
      .Transform([this](EmptyValue) {
        return futures::OnError(
            GetEdgeStateDirectory().Transform(options_.log_supplier),
            [](Error error) {
              LOG(INFO) << "Error opening log: " << error.description;
              return futures::Past(NewNullLog());
            });
      })
      .Transform([this, root_this = ptr_this_->ToRoot()](
                     NonNull<std::unique_ptr<Log>> log) {
        log_ = std::move(log);
        switch (reload_state_) {
          case ReloadState::kDone:
            LOG(FATAL) << "Invalid reload state! Can't be kDone.";
            break;
          case ReloadState::kOngoing:
            reload_state_ = ReloadState::kDone;
            if (fd_ == nullptr && fd_error_ == nullptr) {
              EndOfFile();
            }
            break;
          case ReloadState::kPending:
            reload_state_ = ReloadState::kDone;
            if (fd_ == nullptr && fd_error_ == nullptr) {
              EndOfFile();
            }
            Reload();
        }
        LOG(INFO) << "Reload finished evaluation: " << name();
        return Success();
      });
}

futures::Value<PossibleError> OpenBuffer::Save() {
  LOG(INFO) << "Saving buffer: " << Read(buffer_variables::name);
  if (options_.handle_save == nullptr) {
    status_.SetWarningText(L"Buffer can't be saved.");
    return futures::Past(PossibleError(Error(L"Buffer can't be saved.")));
  }
  return options_.handle_save(
      {.buffer = *this, .save_type = Options::SaveType::kMainFile});
}

futures::ValueOrError<Path> OpenBuffer::GetEdgeStateDirectory() const {
  auto path_vector = editor().edge_path();
  if (path_vector.empty()) {
    return futures::Past(ValueOrError<Path>(Error(L"Empty edge path.")));
  }
  auto file_path = AugmentErrors(
      std::wstring{L"Unable to persist buffer with invalid path "} +
          (dirty() ? L" (dirty)" : L" (clean)") + L" " +
          (disk_state_ == DiskState::kStale ? L"modified" : L"not modified"),
      AbsolutePath::FromString(Read(buffer_variables::path)));
  if (file_path.IsError()) {
    return futures::Past(ValueOrError<Path>(file_path.error()));
  }

  if (file_path.value().GetRootType() != Path::RootType::kAbsolute) {
    return futures::Past(ValueOrError<Path>(
        Error(L"Unable to persist buffer without absolute path: " +
              file_path.value().read())));
  }

  auto file_path_components = AugmentErrors(L"Unable to split path",
                                            file_path.value().DirectorySplit());
  if (file_path_components.IsError()) {
    return futures::Past(ValueOrError<Path>(file_path_components.error()));
  }

  file_path_components.value().push_front(
      PathComponent::FromString(L"state").value());

  auto path = std::make_shared<Path>(path_vector[0]);
  auto error = std::make_shared<std::optional<Error>>();
  LOG(INFO) << "GetEdgeStateDirectory: Preparing state directory: " << *path;
  return futures::ForEachWithCopy(
             file_path_components.value().begin(),
             file_path_components.value().end(),
             [this, path, error](auto component) {
               *path = Path::Join(*path, component);
               return file_system_driver_.Stat(*path)
                   .Transform([path, error](struct stat stat_buffer) {
                     if (S_ISDIR(stat_buffer.st_mode)) {
                       return Success(IterationControlCommand::kContinue);
                     }
                     *error = Error(L"Oops, exists, but is not a directory: " +
                                    path->read());
                     return Success(IterationControlCommand::kStop);
                   })
                   .ConsumeErrors([this, path, error](Error) {
                     return file_system_driver_.Mkdir(*path, 0700)
                         .Transform([](EmptyValue) {
                           return Success(IterationControlCommand::kContinue);
                         })
                         .ConsumeErrors([path, error](Error mkdir_error) {
                           *error = mkdir_error;
                           return futures::Past(IterationControlCommand::kStop);
                         });
                   });
             })
      .Transform([path, error](IterationControlCommand) -> ValueOrError<Path> {
        if (error->has_value()) return error->value();
        return *path;
      });
}

Log& OpenBuffer::log() const { return log_.value(); }

void OpenBuffer::UpdateBackup() {
  CHECK(backup_state_ == DiskState::kStale);
  log_->Append(L"UpdateBackup starts.");
  if (options_.handle_save != nullptr) {
    options_.handle_save(
        {.buffer = *this, .save_type = Options::SaveType::kBackup});
  }
  backup_state_ = DiskState::kCurrent;
}

BufferDisplayData& OpenBuffer::display_data() { return display_data_.value(); }
const BufferDisplayData& OpenBuffer::display_data() const {
  return display_data_.value();
}

void OpenBuffer::AppendLazyString(NonNull<std::shared_ptr<LazyString>> input) {
  ColumnNumber start;
  ForEachColumn(input.value(), [&](ColumnNumber i, wchar_t c) {
    CHECK_GE(i, start);
    if (c == '\n') {
      AppendLine(Substring(input, start, i - start));
      start = i + ColumnNumberDelta(1);
    }
  });
  AppendLine(Substring(input, start));
}

// TODO: WTF is this?
static void AddToParseTree(const NonNull<shared_ptr<LazyString>>& str_input) {
  wstring str = str_input->ToString();
}

void OpenBuffer::SortContents(
    LineNumber first, LineNumber last,
    std::function<bool(const NonNull<std::shared_ptr<const Line>>&,
                       const NonNull<std::shared_ptr<const Line>>&)>
        compare) {
  CHECK(first <= last);
  contents_.sort(first, last, compare);
}

LineNumberDelta OpenBuffer::lines_size() const { return contents_.size(); }

LineNumber OpenBuffer::EndLine() const { return contents_.EndLine(); }

EditorMode& OpenBuffer::mode() const { return mode_.value(); }

language::NonNull<std::shared_ptr<MapModeCommands>>
OpenBuffer::default_commands() {
  return default_commands_;
}

void OpenBuffer::EraseLines(LineNumber first, LineNumber last) {
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + contents_.size());
  contents_.EraseLines(first, last, BufferContents::CursorsBehavior::kAdjust);
}

void OpenBuffer::InsertLine(LineNumber line_position,
                            NonNull<std::shared_ptr<Line>> line) {
  contents_.insert_line(line_position, AddLineMetadata(*this, std::move(line)));
}

void OpenBuffer::AppendLine(NonNull<std::shared_ptr<LazyString>> str) {
  if (reading_from_parser_) {
    switch (str->get(ColumnNumber(0))) {
      case 'E':
        return AppendRawLine(Substring(str, ColumnNumber(1)));

      case 'T':
        AddToParseTree(str);
        return;
    }
    return;
  }

  if (contents_.size() == LineNumberDelta(1) &&
      contents_.back()->EndColumn().IsZero()) {
    if (str->ToString() == L"EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return;
    }
  }

  AppendRawLine(str);
}

void OpenBuffer::AppendRawLine(NonNull<std::shared_ptr<LazyString>> str) {
  AppendRawLine(MakeNonNullShared<Line>(Line::Options(std::move(str))));
}

void OpenBuffer::AppendRawLine(NonNull<std::shared_ptr<Line>> line) {
  auto follower = GetEndPositionFollower();
  contents_.push_back(AddLineMetadata(*this, std::move(line)));
}

void OpenBuffer::AppendToLastLine(NonNull<std::shared_ptr<LazyString>> str) {
  AppendToLastLine(Line(Line::Options(std::move(str))));
}

void OpenBuffer::AppendToLastLine(Line line) {
  static Tracker tracker(L"OpenBuffer::AppendToLastLine");
  auto tracker_call = tracker.Call();
  auto follower = GetEndPositionFollower();
  Line::Options options = contents_.back()->CopyOptions();
  options.Append(line);
  AppendRawLine(MakeNonNullShared<Line>(std::move(options)));
  contents_.EraseLines(contents_.EndLine() - LineNumberDelta(1),
                       contents_.EndLine(),
                       BufferContents::CursorsBehavior::kUnmodified);
}

ValueOrError<
    std::pair<NonNull<std::unique_ptr<Expression>>, gc::Root<Environment>>>
OpenBuffer::CompileString(const std::wstring& code) {
  gc::Root<Environment> sub_environment =
      editor().gc_pool().NewRoot(MakeNonNullUnique<Environment>(environment_));
  auto compilation_result =
      afc::vm::CompileString(code, editor().gc_pool(), sub_environment);
  if (compilation_result.IsError()) return compilation_result.error();
  return std::make_pair(std::move(compilation_result.value()), sub_environment);
}

futures::ValueOrError<gc::Root<Value>> OpenBuffer::EvaluateExpression(
    Expression& expr, gc::Root<Environment> environment) {
  return Evaluate(expr, editor().gc_pool(), environment,
                  [work_queue = work_queue(), root_this = ptr_this_->ToRoot()](
                      std::function<void()> callback) {
                    work_queue->Schedule(std::move(callback));
                  });
}

futures::ValueOrError<gc::Root<Value>> OpenBuffer::EvaluateString(
    const wstring& code) {
  LOG(INFO) << "Compiling code.";
  ValueOrError<
      std::pair<NonNull<std::unique_ptr<Expression>>, gc::Root<Environment>>>
      compilation_result = CompileString(code);
  if (compilation_result.IsError()) {
    Error error =
        Error::Augment(L"🐜Compilation error", compilation_result.error());
    status_.SetWarningText(error.description);
    return futures::Past(ValueOrError<gc::Root<Value>>(std::move(error)));
  }
  auto [expression, environment] = std::move(compilation_result.value());
  LOG(INFO) << "Code compiled, evaluating.";
  return EvaluateExpression(expression.value(), environment);
}

futures::ValueOrError<gc::Root<Value>> OpenBuffer::EvaluateFile(
    const Path& path) {
  ValueOrError<NonNull<std::unique_ptr<Expression>>> expression = CompileFile(
      ToByteString(path.read()), editor().gc_pool(), environment_.ToRoot());
  if (expression.IsError()) {
    Error error =
        Error::Augment(path.read() + L": error: ", expression.error());
    status_.SetWarningText(error.description);
    return futures::Past(error);
  }
  LOG(INFO) << Read(buffer_variables::path) << " ("
            << Read(buffer_variables::name) << "): Evaluating file: " << path;
  return Evaluate(
      expression.value().value(), editor().gc_pool(), environment_.ToRoot(),
      [path, work_queue = work_queue()](std::function<void()> resume) {
        LOG(INFO) << "Evaluation of file yields: " << path;
        work_queue->Schedule(std::move(resume));
      });
}

const NonNull<std::shared_ptr<WorkQueue>>& OpenBuffer::work_queue() const {
  return work_queue_;
}

OpenBuffer::LockFunction OpenBuffer::GetLockFunction() {
  return [root_this =
              ptr_this_->ToRoot()](std::function<void(OpenBuffer&)> callback) {
    root_this.ptr()->work_queue()->Schedule(
        [root_this, callback = std::move(callback)]() {
          callback(root_this.ptr().value());
        });
  };
}

void OpenBuffer::DeleteRange(const Range& range) {
  if (range.begin.line == range.end.line) {
    contents_.DeleteCharactersFromLine(range.begin,
                                       range.end.column - range.begin.column);
    AddLineMetadata(*this, contents_, range.begin.line);
  } else {
    contents_.DeleteToLineEnd(range.begin);
    contents_.DeleteCharactersFromLine(LineColumn(range.end.line),
                                       range.end.column.ToDelta());
    // Lines in the middle.
    EraseLines(range.begin.line + LineNumberDelta(1), range.end.line);
    contents_.FoldNextLine(range.begin.line);
    AddLineMetadata(*this, contents_, range.begin.line);
  }
}

LineColumn OpenBuffer::InsertInPosition(
    const BufferContents& contents_to_insert, const LineColumn& input_position,
    const std::optional<LineModifierSet>& modifiers) {
  VLOG(5) << "InsertInPosition: " << input_position << " "
          << (modifiers.has_value() ? modifiers.value().size() : 1);
  auto blocker = cursors_tracker_.DelayTransformations();
  LineColumn position = input_position;
  if (position.line > contents_.EndLine()) {
    position.line = contents_.EndLine();
    position.column = contents_.at(position.line)->EndColumn();
  }
  if (position.column > contents_.at(position.line)->EndColumn()) {
    position.column = contents_.at(position.line)->EndColumn();
  }
  contents_.SplitLine(position);
  contents_.insert(position.line.next(), contents_to_insert, modifiers);
  contents_.FoldNextLine(position.line);
  AddLineMetadata(*this, contents_, position.line);

  LineNumber last_line =
      position.line + contents_to_insert.size() - LineNumberDelta(1);
  CHECK_LE(last_line, EndLine());
  auto line = LineAt(last_line);
  CHECK(line != nullptr);
  ColumnNumber column = line->EndColumn();

  contents_.FoldNextLine(last_line);
  AddLineMetadata(*this, contents_, last_line);
  return LineColumn(last_line, column);
}

LineColumn OpenBuffer::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(contents_.size(), LineNumberDelta(0));
  position.line = min(position.line, contents_.EndLine());
  CHECK(LineAt(position.line) != nullptr);
  position.column = min(LineAt(position.line)->EndColumn(), position.column);
  return position;
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (current_line() == nullptr) {
    return;
  }
  set_current_position_col(
      std::min(position().column, current_line()->EndColumn()));
}

void OpenBuffer::MaybeExtendLine(LineColumn position) {
  CHECK_LE(position.line, contents_.EndLine());
  auto line = MakeNonNullShared<Line>(contents_.at(position.line).value());
  if (line->EndColumn() > position.column + ColumnNumberDelta(1)) {
    return;
  }
  line->Append(Line(ColumnNumberDelta::PaddingString(
      position.column - line->EndColumn() + ColumnNumberDelta(1), L' ')));

  contents_.set_line(position.line, NonNull(std::move(line)));
}

void OpenBuffer::CheckPosition() {
  if (position().line > contents_.EndLine()) {
    set_position(LineColumn(contents_.EndLine()));
  }
}

CursorsSet& OpenBuffer::FindOrCreateCursors(const wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

const CursorsSet* OpenBuffer::FindCursors(const wstring& name) const {
  return cursors_tracker_.FindCursors(name);
}

CursorsSet& OpenBuffer::active_cursors() {
  return const_cast<CursorsSet&>(
      const_cast<const OpenBuffer*>(this)->active_cursors());
}

const CursorsSet& OpenBuffer::active_cursors() const {
  const CursorsSet* cursors = FindCursors(L"");
  // TODO(easy, 2022-04-30): Find a way to get rid of this check.
  return Pointer(cursors).Reference();
}

void OpenBuffer::set_active_cursors(const vector<LineColumn>& positions) {
  if (positions.empty()) {
    return;
  }
  CursorsSet& cursors = active_cursors();
  FindOrCreateCursors(kOldCursors).swap(&cursors);
  cursors.clear();
  cursors.insert(positions.begin(), positions.end());

  // We find the first position (rather than just take cursors->begin()) so that
  // we start at the first requested position.
  cursors.SetCurrentCursor(positions.front());
}

void OpenBuffer::ToggleActiveCursors() {
  LineColumn desired_position = position();

  CursorsSet& cursors = active_cursors();
  FindOrCreateCursors(kOldCursors).swap(&cursors);

  // TODO: Maybe it'd be best to pick the nearest after the cursor?
  // TODO: This should probably be merged somewhat with set_active_cursors?
  for (auto it = cursors.begin(); it != cursors.end(); ++it) {
    if (desired_position == *it) {
      LOG(INFO) << "Desired position " << desired_position << " prevails.";
      cursors.SetCurrentCursor(desired_position);
      CHECK_LE(position().line, LineNumber(0) + lines_size());
      return;
    }
  }

  cursors.SetCurrentCursor(*cursors.begin());
  LOG(INFO) << "Picked up the first cursor: " << position();
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::PushActiveCursors() {
  auto stack_size = cursors_tracker_.Push();
  status_.SetInformationText(L"cursors stack (" + to_wstring(stack_size) +
                             L"): +");
}

void OpenBuffer::PopActiveCursors() {
  auto stack_size = cursors_tracker_.Pop();
  if (stack_size == 0) {
    status_.SetWarningText(L"cursors stack: -: Stack is empty!");
    return;
  }
  status_.SetInformationText(L"cursors stack (" + to_wstring(stack_size - 1) +
                             L"): -");
}

void OpenBuffer::SetActiveCursorsToMarks() {
  const std::multimap<LineColumn, LineMarks::Mark>& marks = GetLineMarks();
  if (marks.empty()) {
    status_.SetWarningText(L"Buffer has no marks!");
    return;
  }

  std::vector<LineColumn> cursors;
  for (auto& [line_column, mark] : marks) {
    cursors.push_back(line_column);
  }
  set_active_cursors(cursors);
}

void OpenBuffer::set_current_cursor(LineColumn new_value) {
  CursorsSet& cursors = active_cursors();
  // Need to do find + erase because cursors is a multiset; we only want to
  // erase one cursor, rather than all cursors with the current value.
  cursors.erase(position());
  cursors.insert(new_value);
  cursors.SetCurrentCursor(new_value);
}

void OpenBuffer::CreateCursor() {
  if (options_.editor.modifiers().structure == StructureChar()) {
    CHECK_LE(position().line, LineNumber(0) + contents_.size());
    active_cursors().insert(position());
  } else {
    auto structure = options_.editor.modifiers().structure;
    Modifiers tmp_modifiers = options_.editor.modifiers();
    tmp_modifiers.structure = StructureCursor();
    Range range = FindPartialRange(tmp_modifiers, position());
    if (range.IsEmpty()) {
      return;
    }
    options_.editor.set_direction(Direction::kForwards);
    LOG(INFO) << "Range for cursors: " << range;
    while (!range.IsEmpty()) {
      auto tmp_first = range.begin;
      structure->SeekToNext(*this, Direction::kForwards, &tmp_first);
      if (tmp_first > range.begin && tmp_first < range.end) {
        VLOG(5) << "Creating cursor at: " << tmp_first;
        active_cursors().insert(tmp_first);
      }
      if (!structure->SeekToLimit(*this, Direction::kForwards, &tmp_first)) {
        break;
      }
      range.begin = tmp_first;
    }
  }
  status_.SetInformationText(L"Cursor created.");
}

LineColumn OpenBuffer::FindNextCursor(LineColumn position,
                                      const Modifiers& modifiers) {
  LOG(INFO) << "Visiting next cursor: " << modifiers;
  auto direction = modifiers.direction;
  CursorsSet& cursors = active_cursors();
  CHECK(!cursors.empty());

  size_t index = 0;
  auto output = cursors.begin();
  while (output != cursors.end() &&
         (*output < position ||
          (direction == Direction::kForwards && *output == position &&
           std::next(output) != cursors.end() &&
           *std::next(output) == position))) {
    ++output;
    ++index;
  }

  size_t repetitions = modifiers.repetitions.value_or(1) % cursors.size();
  size_t final_position;  // From cursors->begin().
  if (direction == Direction::kForwards) {
    final_position = (index + repetitions) % cursors.size();
  } else if (index >= repetitions) {
    final_position = index - repetitions;
  } else {
    final_position = cursors.size() - (repetitions - index);
  }
  output = cursors.begin();
  std::advance(output, final_position);
  return *output;
}

void OpenBuffer::DestroyCursor() {
  CursorsSet& cursors = active_cursors();
  if (cursors.size() <= 1) {
    return;
  }
  size_t repetitions = min(options_.editor.modifiers().repetitions.value_or(1),
                           cursors.size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors.DeleteCurrentCursor();
  }
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::DestroyOtherCursors() {
  CheckPosition();
  auto position = this->position();
  CHECK_LE(position, LineColumn(LineNumber(0) + contents_.size()));
  CursorsSet& cursors = active_cursors();
  cursors.clear();
  cursors.insert(position);
  Set(buffer_variables::multiple_cursors, false);
}

Range OpenBuffer::FindPartialRange(const Modifiers& modifiers,
                                   LineColumn position) const {
  Range output;
  const auto forward = modifiers.direction;
  const auto backward = ReverseDirection(forward);

  position.line = min(contents_.EndLine(), position.line);
  if (position.column > LineAt(position.line)->EndColumn()) {
    if (Read(buffer_variables::extend_lines)) {
      // TODO: Somehow move this to a more suitable location. Here it clashes
      // with the desired constness of this method.
      // MaybeExtendLine(position);
    } else {
      position.column = LineAt(position.line)->EndColumn();
    }
  }

  if (modifiers.direction == Direction::kBackwards &&
      modifiers.structure != StructureTree()) {
    // TODO: Handle this in structure.
    Seek(contents_, &position).Backwards().WrappingLines().Once();
  }

  output.begin = position;
  LOG(INFO) << "Initial position: " << position
            << ", structure: " << modifiers.structure->ToString();
  if (modifiers.structure->space_behavior() ==
      Structure::SpaceBehavior::kForwards) {
    modifiers.structure->SeekToNext(*this, forward, &output.begin);
  }
  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      output.begin = modifiers.direction == Direction::kForwards
                         ? max(position, output.begin)
                         : min(position, output.begin);
      break;

    case Modifiers::LIMIT_CURRENT: {
      if (modifiers.structure->SeekToLimit(*this, backward, &output.begin)) {
        Seek(contents_, &output.begin)
            .WrappingLines()
            .WithDirection(forward)
            .Once();
      }
    } break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (modifiers.structure->SeekToLimit(*this, backward, &output.begin)) {
        modifiers.structure->SeekToNext(*this, backward, &output.begin);
        modifiers.structure->SeekToLimit(*this, forward, &output.begin);
      }
  }
  LOG(INFO) << "After seek, initial position: " << output.begin;
  output.end = modifiers.direction == Direction::kForwards
                   ? max(position, output.begin)
                   : min(position, output.begin);
  bool move_start = true;
  for (size_t i = 1; i < modifiers.repetitions.value_or(1); i++) {
    LineColumn position = output.end;
    if (!modifiers.structure->SeekToLimit(*this, forward, &output.end)) {
      move_start = false;
      break;
    }
    modifiers.structure->SeekToNext(*this, forward, &output.end);
    if (output.end == position) {
      break;
    }
  }

  LOG(INFO) << "After repetitions: " << output;
  switch (modifiers.boundary_end) {
    case Modifiers::CURRENT_POSITION:
      break;

    case Modifiers::LIMIT_CURRENT:
      move_start &=
          modifiers.structure->SeekToLimit(*this, forward, &output.end);
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      move_start &=
          modifiers.structure->SeekToLimit(*this, forward, &output.end);
      modifiers.structure->SeekToNext(*this, forward, &output.end);
  }
  LOG(INFO) << "After adjusting end: " << output;

  if (output.begin > output.end) {
    CHECK(modifiers.direction == Direction::kBackwards);
    auto tmp = output.end;
    output.end = output.begin;
    output.begin = tmp;
    if (move_start) {
      Seek(contents_, &output.begin).WrappingLines().Once();
    }
  }
  LOG(INFO) << "After wrap: " << output;
  return output;
}

const ParseTree& OpenBuffer::current_tree(const ParseTree& root) const {
  auto route = FindRouteToPosition(root, position());
  if (route.size() < tree_depth_) {
    return root;
  }
  if (route.size() > tree_depth_) {
    route.resize(tree_depth_);
  }
  return FollowRoute(root, route);
}

language::NonNull<std::shared_ptr<const ParseTree>>
OpenBuffer::current_zoomed_out_parse_tree(LineNumberDelta view_size) const {
  return buffer_syntax_parser_.current_zoomed_out_parse_tree(view_size,
                                                             lines_size());
}

std::unique_ptr<BufferTerminal> OpenBuffer::NewTerminal() {
  return std::make_unique<BufferTerminal>(*this, contents_);
}

const std::shared_ptr<const Line> OpenBuffer::current_line() const {
  return LineAt(position().line);
}

std::shared_ptr<const Line> OpenBuffer::LineAt(LineNumber line_number) const {
  if (line_number > contents_.EndLine()) {
    return nullptr;
  }
  return contents_.at(line_number).get_shared();
}

wstring OpenBuffer::ToString() const { return contents_.ToString(); }

const struct timespec OpenBuffer::time_last_exit() const {
  return time_last_exit_;
}

void OpenBuffer::PushSignal(UnixSignal signal) {
  switch (signal.read()) {
    case SIGINT:
      if (terminal_ == nullptr ? child_pid_ == -1 : fd_ == nullptr) {
        status_.SetWarningText(L"No subprocess found.");
      } else if (terminal_ == nullptr) {
        status_.SetInformationText(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, signal.read());
      } else {
        string sequence(1, 0x03);
        (void)write(fd_->fd().read(), sequence.c_str(), sequence.size());
        status_.SetInformationText(L"SIGINT");
      }
      break;

    case SIGTSTP:
      static const string sequence(1, 0x1a);
      if (terminal_ != nullptr && fd_ != nullptr) {
        (void)write(fd_->fd().read(), sequence.c_str(), sequence.size());
      }
      break;

    default:
      status_.SetWarningText(L"Unexpected signal received: " +
                             to_wstring(signal.read()));
  }
}

FileSystemDriver& OpenBuffer::file_system_driver() const {
  return file_system_driver_;
}

futures::Value<std::wstring> OpenBuffer::TransformKeyboardText(
    std::wstring input) {
  using afc::vm::VMType;
  auto input_shared = std::make_shared<std::wstring>(std::move(input));
  return futures::ForEach(
             keyboard_text_transformers_.begin(),
             keyboard_text_transformers_.end(),
             [this, input_shared](const gc::Root<Value>& t) {
               std::vector<gc::Root<Value>> args;
               gc::Pool& pool = editor().gc_pool();
               args.push_back(Value::NewString(pool, std::move(*input_shared)));
               return Call(pool, t.ptr().value(), std::move(args),
                           [work_queue =
                                work_queue()](std::function<void()> callback) {
                             work_queue->Schedule(std::move(callback));
                           })
                   .Transform([input_shared](const gc::Root<Value>& value) {
                     *input_shared = value.ptr()->get_string();
                     return Success(IterationControlCommand::kContinue);
                   })
                   .ConsumeErrors([](Error) {
                     return futures::Past(IterationControlCommand::kContinue);
                   });
               ;
             })
      .Transform([input_shared](IterationControlCommand) {
        return futures::Past(std::move(*input_shared));
      });
}

bool OpenBuffer::AddKeyboardTextTransformer(gc::Root<Value> transformer) {
  VMType& type = transformer.ptr()->type;
  if (type.type != VMType::Type::kFunction || type.type_arguments.size() != 2 ||
      type.type_arguments[0] != VMType::String() ||
      type.type_arguments[1] != VMType::String()) {
    status_.SetWarningText(
        L": Unexpected type for keyboard text transformer: " +
        transformer.ptr()->type.ToString());
    return false;
  }
  keyboard_text_transformers_.push_back(std::move(transformer));
  return true;
}

BufferName OpenBuffer::name() const {
  return BufferName(Read(buffer_variables::name));
}

void OpenBuffer::SetInputFiles(FileDescriptor input_fd,
                               FileDescriptor input_error_fd,
                               bool fd_is_terminal, pid_t child_pid) {
  if (Read(buffer_variables::clear_on_reload)) {
    ClearContents(BufferContents::CursorsBehavior::kUnmodified);
    SetDiskState(DiskState::kCurrent);
  }

  CHECK_EQ(child_pid_, -1);
  terminal_ = fd_is_terminal ? NewTerminal() : nullptr;

  auto new_reader = [this](FileDescriptor fd, LineModifierSet modifiers)
      -> std::unique_ptr<FileDescriptorReader> {
    if (fd == FileDescriptor(-1)) {
      return nullptr;
    }
    return std::make_unique<FileDescriptorReader>(
        FileDescriptorReader::Options{.buffer = *this,
                                      .fd = fd,
                                      .modifiers = std::move(modifiers),
                                      .terminal = terminal_.get(),
                                      .thread_pool = editor().thread_pool()});
  };

  fd_ = new_reader(input_fd, {});
  fd_error_ = new_reader(input_error_fd, {LineModifier::BOLD});

  if (terminal_ != nullptr) {
    terminal_->UpdateSize();
  }

  child_pid_ = child_pid;
}

const FileDescriptorReader* OpenBuffer::fd() const { return fd_.get(); }

const FileDescriptorReader* OpenBuffer::fd_error() const {
  return fd_error_.get();
}

LineNumber OpenBuffer::current_position_line() const { return position().line; }

void OpenBuffer::set_current_position_line(LineNumber line) {
  set_current_cursor(LineColumn(min(line, LineNumber(0) + contents_.size())));
}

ColumnNumber OpenBuffer::current_position_col() const {
  return position().column;
}

void OpenBuffer::set_current_position_col(ColumnNumber column) {
  set_current_cursor(LineColumn(position().line, column));
}

const LineColumn OpenBuffer::position() const {
  return cursors_tracker_.position();
}

void OpenBuffer::set_position(const LineColumn& position) {
  set_current_cursor(position);
}

namespace {
std::vector<URL> GetURLsWithExtensionsForContext(const OpenBuffer& buffer,
                                                 const URL& original_url) {
  std::vector<URL> output = {original_url};
  ValueOrError<Path> path = original_url.GetLocalFilePath();
  if (path.IsError()) return output;
  auto extensions = TokenizeBySpaces(
      NewLazyString(buffer.Read(buffer_variables::file_context_extensions))
          .value());
  for (auto& extension_token : extensions) {
    CHECK(!extension_token.value.empty());
    output.push_back(URL::FromPath(
        Path::WithExtension(path.value(), extension_token.value)));
  }
  return output;
}

ValueOrError<URL> FindLinkTarget(const OpenBuffer& buffer,
                                 const ParseTree& tree) {
  if (tree.properties().find(ParseTreeProperty::LinkTarget()) !=
      tree.properties().end()) {
    auto contents = buffer.contents().copy();
    contents->FilterToRange(tree.range());
    return URL(contents->ToString());
  }
  for (const auto& child : tree.children()) {
    if (auto output = FindLinkTarget(buffer, child); !output.IsError())
      return output;
  }
  return Error(L"Unable to find link.");
}

std::vector<URL> GetURLsForCurrentPosition(const OpenBuffer& buffer) {
  auto adjusted_position = buffer.AdjustLineColumn(buffer.position());
  std::optional<URL> initial_url;

  NonNull<std::shared_ptr<const ParseTree>> tree = buffer.parse_tree();
  ParseTree::Route route = FindRouteToPosition(tree.value(), adjusted_position);
  for (const ParseTree* subtree : MapRoute(tree.value(), route)) {
    if (subtree->properties().find(ParseTreeProperty::Link()) !=
        subtree->properties().end()) {
      if (auto target = FindLinkTarget(buffer, *subtree); !target.IsError()) {
        initial_url = target.value();
        break;
      }
    }
  }

  if (!initial_url.has_value()) {
    std::wstring line = GetCurrentToken(
        {.contents = buffer.contents(),
         .line_column = adjusted_position,
         .token_characters = buffer.Read(buffer_variables::path_characters)});

    if (line.find_first_not_of(L"/.:") == wstring::npos) {
      // If there are only slashes, colons or dots, it's probably not very
      // useful to show the contents of this path.
      return {};
    }

    auto path = Path::FromString(line);
    if (path.IsError()) {
      return {};
    }
    initial_url = URL::FromPath(std::move(path.value()));
  }

  auto urls_with_extensions =
      GetURLsWithExtensionsForContext(buffer, *initial_url);

  std::vector<Path> search_paths = {};
  if (auto path = Path::FromString(buffer.Read(buffer_variables::path));
      !path.IsError()) {
    // Works if the current buffer is a directory listing:
    search_paths.push_back(path.value());
    // And a fall-back for the current buffer being a file:
    if (auto dir = path.value().Dirname(); !dir.IsError()) {
      search_paths.push_back(dir.value());
    }
  }

  std::vector<URL> urls = urls_with_extensions;

  // Do the full expansion. This has square complexity, though luckily the
  // number of local_paths tends to be very small.
  for (const Path& search_path : search_paths) {
    for (const URL& url : urls_with_extensions) {
      ValueOrError<Path> path = url.GetLocalFilePath();
      if (path.IsError() ||
          path.value().GetRootType() == Path::RootType::kAbsolute)
        continue;
      urls.push_back(URL::FromPath(Path::Join(search_path, path.value())));
    }
  }
  return urls;
}

}  // namespace

futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>>
OpenBuffer::OpenBufferForCurrentPosition(
    RemoteURLBehavior remote_url_behavior) {
  // When the cursor moves quickly, there's a race between multiple executions
  // of this logic. To avoid this, each call captures the original position and
  // uses that to avoid taking any effects when the position changes in the
  // meantime.
  auto adjusted_position = AdjustLineColumn(position());
  struct Data {
    const gc::Root<OpenBuffer> source;
    ValueOrError<std::optional<gc::Root<OpenBuffer>>> output = std::nullopt;
  };
  NonNull<std::shared_ptr<Data>> data =
      MakeNonNullShared<Data>(Data{.source = ptr_this_->ToRoot()});

  return futures::ForEach(
             std::make_shared<std::vector<URL>>(
                 GetURLsForCurrentPosition(*this)),
             [adjusted_position, data, remote_url_behavior](const URL& url) {
               auto& editor = data->source.ptr()->editor();
               VLOG(5) << "Checking URL: " << url.ToString();
               if (url.schema().value_or(URL::Schema::kFile) !=
                   URL::Schema::kFile) {
                 switch (remote_url_behavior) {
                   case RemoteURLBehavior::kIgnore:
                     break;
                   case RemoteURLBehavior::kLaunchBrowser:
                     editor.work_queue()->ScheduleAt(
                         AddSeconds(Now(), 1.0),
                         [status_expiration =
                              std::shared_ptr<StatusExpirationControl>(
                                  editor.status().SetExpiringInformationText(
                                      L"Open: " + url.ToString()))] {});
                     ForkCommand(editor,
                                 ForkCommandOptions{
                                     .command = L"xdg-open " +
                                                ShellEscape(url.ToString()),
                                     .insertion_type =
                                         BuffersList::AddBufferType::kIgnore,
                                 });
                 }
                 return futures::Past(futures::IterationControlCommand::kStop);
               }
               ValueOrError<Path> path = url.GetLocalFilePath();
               if (path.IsError())
                 return futures::Past(
                     futures::IterationControlCommand::kContinue);
               VLOG(4) << "Calling open file: " << path.value().read();
               return OpenFileIfFound(
                          OpenFileOptions{
                              .editor_state = editor,
                              .path = path.value(),
                              .insertion_type =
                                  BuffersList::AddBufferType::kIgnore,
                              .use_search_paths = false})
                   .Transform([data](gc::Root<OpenBuffer> buffer_context) {
                     data->output = buffer_context;
                     return futures::Past(
                         Success(futures::IterationControlCommand::kStop));
                   })
                   .ConsumeErrors([adjusted_position, data](Error) {
                     if (adjusted_position !=
                         data->source.ptr()->AdjustLineColumn(
                             data->source.ptr()->position())) {
                       data->output = Error(L"Computation was cancelled.");
                       return futures::Past(
                           futures::IterationControlCommand::kStop);
                     }
                     return futures::Past(
                         futures::IterationControlCommand::kContinue);
                   });
             })
      .Transform([data](IterationControlCommand iteration_control_command) {
        return iteration_control_command ==
                       futures::IterationControlCommand::kContinue
                   ? Success(std::optional<gc::Root<OpenBuffer>>())
                   : std::move(data->output);
      });
}

LineColumn OpenBuffer::end_position() const {
  CHECK_GT(contents_.size(), LineNumberDelta(0));
  return LineColumn(contents_.EndLine(), contents_.back()->EndColumn());
}

std::unique_ptr<OpenBuffer::DiskState,
                std::function<void(OpenBuffer::DiskState*)>>
OpenBuffer::FreezeDiskState() {
  return std::unique_ptr<DiskState,
                         std::function<void(OpenBuffer::DiskState*)>>(
      new DiskState(disk_state_), [this](DiskState* old_state) {
        SetDiskState(Pointer(old_state).Reference());
        delete old_state;
      });
}

bool OpenBuffer::dirty() const {
  return (disk_state_ == DiskState::kStale &&
          (!Read(buffer_variables::path).empty() ||
           !contents().EveryLine(
               [](LineNumber, const Line& l) { return l.empty(); }))) ||
         child_pid_ != -1 ||
         (child_exit_status_.has_value() &&
          (!WIFEXITED(child_exit_status_.value()) ||
           WEXITSTATUS(child_exit_status_.value()) != 0));
}

std::map<wstring, wstring> OpenBuffer::Flags() const {
  std::map<wstring, wstring> output;
  if (options_.describe_status) {
    output = options_.describe_status(*this);
  }

  if (disk_state() == DiskState::kStale) {
    output.insert({L"🐾", L""});
  }

  if (ShouldDisplayProgress()) {
    output.insert({ProgressString(Read(buffer_variables::progress),
                                  OverflowBehavior::kModulo),
                   L""});
  }

  if (fd() != nullptr) {
    output.insert({L"<", L""});
    switch (contents_.size().line_delta) {
      case 1:
        output.insert({L"⚊", L""});
        break;
      case 2:
        output.insert({L"⚌ ", L""});
        break;
      case 3:
        output.insert({L"☰ ", L""});
        break;
      default:
        output.insert({L"☰ ", contents_.EndLine().ToUserString()});
    }
    if (Read(buffer_variables::follow_end_of_file)) {
      output.insert({L"↓", L""});
    }
    wstring pts_path = Read(buffer_variables::pts_path);
    if (!pts_path.empty()) {
      output.insert({L"💻", pts_path});
    }
  }

  if (work_queue()->RecentUtilization() > 0.1) {
    output.insert({L"⏳", L""});
  }

  if (Read(buffer_variables::pin)) {
    output.insert({L"📌", L""});
  }

  if (child_pid_ != -1) {
    output.insert({L"🤴", std::to_wstring(child_pid_)});
  } else if (!child_exit_status_.has_value()) {
    // Nothing.
  } else if (WIFEXITED(child_exit_status_.value())) {
    output.insert(
        {L"exit", std::to_wstring(WEXITSTATUS(child_exit_status_.value()))});
  } else if (WIFSIGNALED(child_exit_status_.value())) {
    output.insert(
        {L"signal", std::to_wstring(WTERMSIG(child_exit_status_.value()))});
  } else {
    output.insert(
        {L"exit-status", std::to_wstring(child_exit_status_.value())});
  }

  auto marks = GetLineMarksText();
  if (!marks.empty()) {
    output.insert({marks, L""});  // TODO: Show better?
  }

  return output;
}

/* static */ wstring OpenBuffer::FlagsToString(
    std::map<wstring, wstring> flags) {
  wstring output;
  wstring separator = L"";
  for (auto& f : flags) {
    output += separator + f.first + f.second;
    separator = L"  ";
  }
  return output;
}

const bool& OpenBuffer::Read(const EdgeVariable<bool>* variable) const {
  return bool_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<bool>* variable, bool value) {
  bool_variables_.Set(variable, value);
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  Set(variable, editor().modifiers().repetitions.has_value()
                    ? editor().modifiers().repetitions != 0
                    : !Read(variable));
}

const wstring& OpenBuffer::Read(const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<wstring>* variable, wstring value) {
  string_variables_.Set(variable, value);
}

const int& OpenBuffer::Read(const EdgeVariable<int>* variable) const {
  return int_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<int>* variable, int value) {
  int_variables_.Set(variable, value);
}

const double& OpenBuffer::Read(const EdgeVariable<double>* variable) const {
  return double_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<double>* variable, double value) {
  double_variables_.Set(variable, value);
}

const LineColumn& OpenBuffer::Read(
    const EdgeVariable<LineColumn>* variable) const {
  return line_column_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<LineColumn>* variable,
                     LineColumn value) {
  line_column_variables_.Set(variable, value);
}

futures::Value<EmptyValue> OpenBuffer::ApplyToCursors(
    transformation::Variant transformation) {
  return ApplyToCursors(std::move(transformation),
                        Read(buffer_variables::multiple_cursors)
                            ? Modifiers::CursorsAffected::kAll
                            : Modifiers::CursorsAffected::kOnlyCurrent,
                        transformation::Input::Mode::kFinal);
}

void StartAdjustingStatusContext(gc::Root<OpenBuffer> buffer) {
  buffer.ptr()
      ->OpenBufferForCurrentPosition(OpenBuffer::RemoteURLBehavior::kIgnore)
      .Transform([buffer](std::optional<gc::Root<OpenBuffer>> result) {
        buffer.ptr()->status().set_context(result);
        return Success();
      });
}

futures::Value<EmptyValue> OpenBuffer::ApplyToCursors(
    transformation::Variant transformation,
    Modifiers::CursorsAffected cursors_affected,
    transformation::Input::Mode mode) {
  auto trace = log_->NewChild(L"ApplyToCursors transformation.");
  trace->Append(L"Transformation: " + transformation::ToString(transformation));
  undo_future_.clear();

  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->PushBack(transformation);
    if (undo_past_.empty()) {
      VLOG(5) << "last_transformation_stack_ has values but undo_past_ is "
                 "empty. This is expected to be very rare. Most likely this "
                 "means that contents were cleared while the stack wasn't "
                 "empty (perhaps because the buffer was reloaded while some "
                 "active transformation/mode was being executed.";
      undo_past_.push_back(MakeNonNullUnique<transformation::Stack>());
    }
  } else {
    undo_past_.push_back(MakeNonNullUnique<transformation::Stack>());
  }

  undo_past_.back()->PushFront(transformation::Cursors{
      .cursors = active_cursors(), .active = position()});

  std::optional<futures::Value<EmptyValue>> transformation_result;
  if (cursors_affected == Modifiers::CursorsAffected::kAll) {
    CursorsSet single_cursor;
    CursorsSet& cursors = active_cursors();
    transformation_result = cursors_tracker_.ApplyTransformationToCursors(
        cursors, [root_this = ptr_this_->ToRoot(),
                  transformation = std::move(transformation),
                  mode](LineColumn position) {
          return root_this.ptr()
              ->Apply(transformation, position, mode)
              .Transform([root_this](transformation::Result result) {
                root_this.ptr()->UpdateLastAction();
                return result.position;
              });
        });
  } else {
    VLOG(6) << "Adjusting default cursor (!multiple_cursors).";
    transformation_result =
        Apply(std::move(transformation), position(), mode)
            .Transform([root_this = ptr_this_->ToRoot()](
                           const transformation::Result& result) {
              root_this.ptr()->active_cursors().MoveCurrentCursor(
                  result.position);
              root_this.ptr()->UpdateLastAction();
              return EmptyValue();
            });
  }
  return transformation_result.value().Transform(
      [root_this = ptr_this_->ToRoot()](EmptyValue) {
        // This proceeds in the background but we can only start it once the
        // transformation is evaluated (since we don't know the cursor position
        // otherwise).
        StartAdjustingStatusContext(root_this);
        return EmptyValue();
      });
}

futures::Value<typename transformation::Result> OpenBuffer::Apply(
    transformation::Variant transformation, LineColumn position,
    transformation::Input::Mode mode) {
  CHECK(!undo_past_.empty());
  const std::weak_ptr<transformation::Stack> undo_stack_weak =
      undo_past_.back().get_shared();

  transformation::Input input(*this);
  input.mode = mode;
  input.position = position;
  if (Read(buffer_variables::delete_into_paste_buffer)) {
    input.delete_buffer =
        &editor()
             .FindOrBuildBuffer(
                 kFuturePasteBuffer,
                 [&] {
                   LOG(INFO) << "Creating paste buffer.";
                   return OpenBuffer::New(
                       {.editor = editor(), .name = kFuturePasteBuffer});
                 })
             .ptr()
             .value();
    CHECK(input.delete_buffer != nullptr);
  } else {
    CHECK_EQ(input.delete_buffer, nullptr);
  }

  VLOG(5) << "Apply transformation: "
          << transformation::ToString(transformation);

  return transformation::Apply(transformation, std::move(input))
      .Transform([this, transformation = std::move(transformation), mode,
                  undo_stack_weak](transformation::Result result) {
        VLOG(6) << "Got results of transformation: "
                << transformation::ToString(transformation);
        if (mode == transformation::Input::Mode::kFinal &&
            Read(buffer_variables::delete_into_paste_buffer)) {
          if (!result.added_to_paste_buffer) {
            editor().buffers()->erase(kFuturePasteBuffer);
          } else if (auto paste_buffer =
                         editor().buffers()->find(kFuturePasteBuffer);
                     paste_buffer != editor().buffers()->end()) {
            editor().buffers()->insert_or_assign(BufferName::PasteBuffer(),
                                                 paste_buffer->second);
          }
        }

        if (result.modified_buffer &&
            mode == transformation::Input::Mode::kFinal) {
          editor().StartHandlingInterrupts();
          last_transformation_ = std::move(transformation);
        }

        if (auto undo_stack = undo_stack_weak.lock(); undo_stack != nullptr) {
          undo_stack->PushFront(
              transformation::Stack{.stack = result.undo_stack->stack});
          *undo_stack = transformation::Stack{
              .stack = {OptimizeBase(std::move(*undo_stack))}};
        }
        return result;
      });
}

futures::Value<EmptyValue> OpenBuffer::RepeatLastTransformation() {
  size_t repetitions = options_.editor.repetitions().value_or(1);
  options_.editor.ResetRepetitions();
  return ApplyToCursors(transformation::Repetitions{
      repetitions,
      std::make_shared<transformation::Variant>(last_transformation_)});
}

void OpenBuffer::PushTransformationStack() {
  if (last_transformation_stack_.empty()) {
    undo_past_.push_back(MakeNonNullUnique<transformation::Stack>());
  }
  last_transformation_stack_.push_back({});
}

void OpenBuffer::PopTransformationStack() {
  if (last_transformation_stack_.empty()) {
    // This can happen if the transformation stack was reset during the
    // evaluation of a transformation. For example, during an insertion, if the
    // buffer is reloaded ... that will discard the transformation stack.
    return;
  }
  last_transformation_ = std::move(last_transformation_stack_.back().value());
  last_transformation_stack_.pop_back();
  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->PushBack(last_transformation_);
  }
}

futures::Value<EmptyValue> OpenBuffer::Undo(UndoMode undo_mode) {
  struct Data {
    std::list<NonNull<std::shared_ptr<transformation::Stack>>>* source;
    std::list<NonNull<std::shared_ptr<transformation::Stack>>>* target;
    size_t repetitions = 0;
  };
  auto data = std::make_shared<Data>();
  if (editor().direction() == Direction::kForwards) {
    data->source = &undo_past_;
    data->target = &undo_future_;
  } else {
    data->source = &undo_future_;
    data->target = &undo_past_;
  }
  return futures::While([this, undo_mode, data] {
           if (data->repetitions == editor().repetitions().value_or(1) ||
               data->source->empty()) {
             return futures::Past(IterationControlCommand::kStop);
           }
           transformation::Input input(*this);
           input.position = position();
           // We've undone the entire changes, so...
           last_transformation_stack_.clear();
           VLOG(5) << "Undo transformation: "
                   << ToStringBase(data->source->back().value());
           return transformation::Apply(data->source->back().value(), input)
               .Transform(
                   [this, undo_mode, data](transformation::Result result) {
                     data->target->push_back(std::move(result.undo_stack));
                     data->source->pop_back();
                     if (result.modified_buffer ||
                         undo_mode == OpenBuffer::UndoMode::kOnlyOne) {
                       data->repetitions++;
                     }
                     return IterationControlCommand::kContinue;
                   });
         })
      .Transform([root_this = ptr_this_->ToRoot()](IterationControlCommand) {
        StartAdjustingStatusContext(root_this);
        return EmptyValue();
      });
}

void OpenBuffer::set_filter(gc::Root<Value> filter) {
  filter_ = std::move(filter);
  filter_version_++;
}

language::gc::Root<OpenBuffer> OpenBuffer::NewRoot() {
  CHECK(ptr_this_.has_value());
  return ptr_this_->ToRoot();
}

language::gc::Root<const OpenBuffer> OpenBuffer::NewRoot() const {
  CHECK(ptr_this_.has_value());
  language::gc::Root<const OpenBuffer> output = ptr_this_->ToRoot();
  return output;
}

const std::multimap<LineColumn, LineMarks::Mark>& OpenBuffer::GetLineMarks()
    const {
  return editor().line_marks().GetMarksForTargetBuffer(name());
}

const std::multimap<LineColumn, LineMarks::ExpiredMark>&
OpenBuffer::GetExpiredLineMarks() const {
  return editor().line_marks().GetExpiredMarksForTargetBuffer(name());
}

wstring OpenBuffer::GetLineMarksText() const {
  size_t marks = GetLineMarks().size();
  size_t expired_marks = GetExpiredLineMarks().size();
  wstring output;
  if (marks > 0 || expired_marks > 0) {
    output = L"marks:" + to_wstring(marks);
    if (expired_marks > 0) {
      output += L"(" + to_wstring(expired_marks) + L")";
    }
  }
  return output;
}

bool OpenBuffer::IsPastPosition(LineColumn position) const {
  return position != LineColumn::Max() &&
         (position.line < contents_.EndLine() ||
          (position.line == contents_.EndLine() &&
           position.column <= LineAt(position.line)->EndColumn()));
}

void OpenBuffer::ReadData(std::unique_ptr<FileDescriptorReader>& source) {
  CHECK(source != nullptr);
  source->ReadData().SetConsumer(
      [this, &source](FileDescriptorReader::ReadResult value) {
        if (value != FileDescriptorReader::ReadResult::kDone) return;
        RegisterProgress();
        source = nullptr;
        if (fd_ == nullptr && fd_error_ == nullptr) {
          EndOfFile();
        }
      });
}

void OpenBuffer::UpdateLastAction() {
  auto now = Now();
  if (now < last_action_) return;
  last_action_ = now;
  if (double idle_seconds = Read(buffer_variables::close_after_idle_seconds);
      idle_seconds >= 0.0) {
    work_queue_->ScheduleAt(
        AddSeconds(Now(), idle_seconds),
        [weak_this = ptr_this_->ToWeakPtr(), last_action = last_action_] {
          VisitPointer(
              weak_this.Lock(),
              [last_action](gc::Root<OpenBuffer> buffer_root) {
                OpenBuffer& buffer = buffer_root.ptr().value();
                if (buffer.last_action_ != last_action) return;
                buffer.last_action_ = Now();
                LOG(INFO) << "close_after_idle_seconds: Closing.";
                buffer.editor().CloseBuffer(buffer);
              },
              [] {});
        });
  }
}

EditorState& EditorForTests() {
  static audio::Player* player = audio::NewNullPlayer().get_unique().release();
  static EditorState editor_for_tests(
      [] {
        CommandLineValues output;
        output.config_paths = {L"/home/edge-test-user/.edge/"};
        return output;
      }(),
      *player);
  return editor_for_tests;
}

gc::Root<OpenBuffer> NewBufferForTests() {
  gc::Root<OpenBuffer> output = OpenBuffer::New(
      {.editor = EditorForTests(),
       .name = EditorForTests().GetUnusedBufferName(L"test buffer")});
  EditorForTests().buffers()->insert_or_assign(output.ptr()->name(), output);
  return output;
}

}  // namespace afc::editor
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const editor::OpenBuffer& buffer) {
  return std::vector<language::NonNull<std::shared_ptr<ControlFrame>>>(
      {buffer.environment().control_frame()});
}
}  // namespace afc::language::gc
