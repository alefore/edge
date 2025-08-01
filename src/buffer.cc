#include "src/buffer.h"

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <ranges>
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
#include "src/buffer_registry.h"
#include "src/buffer_transformation_adapter.h"
#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/regular_file_adapter.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/gc_util.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/observers_gc.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/text/delegating_mutable_line_sequence_observer.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/language/wstring.h"
#include "src/line_marks.h"
#include "src/map_mode.h"
#include "src/run_command_handler.h"
#include "src/seek.h"
#include "src/server.h"
#include "src/status.h"
#include "src/transformation.h"
#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/stack.h"
#include "src/url.h"
#include "src/vm/callbacks.h"
#include "src/vm/constant_expression.h"
#include "src/vm/escape.h"
#include "src/vm/function_call.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
namespace audio = afc::infrastructure::audio;
namespace container = afc::language::container;

using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::concurrent::WorkQueue;
using afc::futures::IterationControlCommand;
using afc::futures::OnError;
using afc::infrastructure::AbsolutePath;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::FileAdapter;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::ProcessId;
using afc::infrastructure::RegularFileAdapter;
using afc::infrastructure::TerminalAdapter;
using afc::infrastructure::UnixSignal;
using afc::infrastructure::UpdateIfMillisecondsHavePassed;
using afc::infrastructure::screen::CursorsSet;
using afc::infrastructure::screen::CursorsTracker;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::VisualOverlay;
using afc::infrastructure::screen::VisualOverlayKey;
using afc::infrastructure::screen::VisualOverlayMap;
using afc::infrastructure::screen::VisualOverlayPriority;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::GetSetWithKeys;
using afc::language::IgnoreErrors;
using afc::language::InsertOrDie;
using afc::language::LazyValue;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ObservableValue;
using afc::language::Observers;
using afc::language::OnceOnlyFunction;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::Pointer;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::WeakPtrLockingObserver;
using afc::language::WrapAsLazyValue;
using afc::language::container::MaterializeUnorderedSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::DelegatingMutableLineSequenceObserver;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineMetadataKey;
using afc::language::text::LineMetadataMap;
using afc::language::text::LineMetadataValue;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineProcessorInput;
using afc::language::text::LineProcessorKey;
using afc::language::text::LineProcessorMap;
using afc::language::text::LineProcessorOutput;
using afc::language::text::LineProcessorOutputFuture;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::MutableLineSequenceObserver;
using afc::language::text::Range;
using afc::language::text::SortedLineSequence;

namespace afc::editor {
namespace {
static const wchar_t* kOldCursors = L"old-cursors";

std::vector<Line> UpdateLineMetadata(OpenBuffer& buffer,
                                     const LineProcessorMap& line_processor_map,
                                     std::vector<Line> lines) {
  if (!buffer.Read(buffer_variables::vm_lines_evaluation)) return lines;

  VLOG(8) << "UpdateLineMetadata: " << buffer.name()
          << " lines: " << lines.size();
  TRACK_OPERATION(OpenBuffer_UpdateLineMetadata);
  for (Line& line : lines)
    if (!line.empty() &&
        (!line.metadata().has_value() || line.metadata().get().empty())) {
      LineBuilder line_builder(std::move(line));
      line_builder.SetMetadata(LazyValue<LineMetadataMap>(
          [&line_processor_map, contents = line_builder.contents()] {
            TRACK_OPERATION(OpenBuffer_UpdateLineMetadata_Run);
            std::map<LineProcessorKey, LineProcessorOutputFuture> output =
                line_processor_map.Process(LineProcessorInput(contents.read()));
            LineMetadataMap line_metadata_map;
            for (const auto& p : output)
              InsertOrDie(
                  line_metadata_map,
                  {LineMetadataKey{p.first.read()},
                   LineMetadataValue{
                       .initial_value = p.second.initial_value.read(),
                       .value =
                           std::move(p.second.value)
                               .ToFuture()
                               .Transform([](LineProcessorOutput output_value) {
                                 return output_value.read();
                               })}});
            return line_metadata_map;
          }));
      line = std::move(line_builder).Build();
    }
  return lines;
}

ValueOrError<LineProcessorOutputFuture> LineMetadataCompilation(
    OpenBuffer& buffer, const LineProcessorInput& input) {
  TRACK_OPERATION(OpenBuffer_LineMetadataCompilation);
  static const LineProcessorOutputFuture kEmptyOutput{
      .initial_value = LineProcessorOutput(SingleLine{}),
      .value = futures::Past(LineProcessorOutput(SingleLine{}))};
  return std::visit(
      overload{[&](ExecutionContext::CompilationResult compilation_result)
                   -> ValueOrError<LineProcessorOutputFuture> {
                 LineProcessorOutputFuture output{
                     .initial_value = LineProcessorOutput(
                         SINGLE_LINE_CONSTANT(L"C++: ") +
                         vm::TypesToString(
                             compilation_result.expression()->Types())),
                     .value = futures::Future<LineProcessorOutput>().value};
                 if (!compilation_result.expression()
                          ->purity()
                          .writes_external_outputs) {
                   output.initial_value =
                       LineProcessorOutput(output.initial_value.read() +
                                           SINGLE_LINE_CONSTANT(L" ..."));
                   if (compilation_result.expression()->Types() ==
                       std::vector<vm::Type>({vm::types::Void{}}))
                     return kEmptyOutput;
                   output.value = buffer.work_queue()->Wait(Now()).Transform(
                       [compilation_result =
                            std::move(compilation_result)](EmptyValue) mutable {
                         return compilation_result.evaluate()
                             .Transform([](gc::Root<vm::Value> value) {
                               std::ostringstream oss;
                               oss << value.ptr().value();
                               return LineProcessorOutput::New(SingleLine::New(
                                   LazyString{FromByteString(oss.str())}));
                             })
                             .ConsumeErrors([](Error error) {
                               return futures::Past(LineProcessorOutput(
                                   SINGLE_LINE_CONSTANT(L"E: ") +
                                   LineSequence::BreakLines(error.read())
                                       .FoldLines()));
                             });
                       });
                 }
                 return output;
               },
               [](Error error) -> ValueOrError<LineProcessorOutputFuture> {
                 return error;
               }},
      buffer.execution_context()->CompileString(
          input.read(), ExecutionContext::ErrorHandling::kIgnore));
}

// We receive `contents` explicitly since `buffer` only gives us const access.
void SetMutableLineSequenceLineMetadata(
    OpenBuffer& buffer, const LineProcessorMap& line_processor_map,
    MutableLineSequence& contents, LineNumber position) {
  contents.set_line(position,
                    UpdateLineMetadata(buffer, line_processor_map,
                                       {buffer.contents().at(position)})[0]);
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
    parent_work_queue->Schedule(WorkQueue::Callback{
        .time = next.value(),
        .callback = [work_queue, parent_work_queue,
                     next_scheduled_execution]() mutable {
          next_scheduled_execution.value() = std::nullopt;
          work_queue->Execute();
          MaybeScheduleNextWorkQueueExecution(work_queue, parent_work_queue,
                                              next_scheduled_execution);
        }});
  }
  return Observers::State::kAlive;
}
}  // namespace

using namespace afc::vm;

/* static */ gc::Root<OpenBuffer> OpenBuffer::New(Options options) {
  gc::Root<MapModeCommands> default_commands =
      options.editor.default_commands()->NewChild();
  gc::Root<MapMode> mode =
      MapMode::New(options.editor.gc_pool(), default_commands.ptr());
  auto status = MakeNonNullShared<Status>(options.editor.audio_player());
  gc::Root<ExecutionContext> execution_context = ExecutionContext::New(
      Environment::New(options.editor.execution_context()->environment()).ptr(),
      status.get_shared(), WorkQueue::New(),
      MakeNonNullUnique<FileSystemDriver>(options.editor.thread_pool()));
  gc::Root<OpenBuffer> output =
      options.editor.gc_pool().NewRoot(MakeNonNullUnique<OpenBuffer>(
          ConstructorAccessTag(), std::move(options), default_commands.ptr(),
          mode.ptr(), std::move(status), execution_context.ptr()));
  output->Initialize(output.ptr());
  return output;
}

class OpenBufferMutableLineSequenceObserver
    : public MutableLineSequenceObserver {
 public:
  void SetOpenBuffer(gc::WeakPtr<OpenBuffer> buffer) { buffer_ = buffer; }

  void LinesInserted(LineNumber, LineNumberDelta) override { Notify(); }

  void LinesErased(LineNumber, LineNumberDelta) override { Notify(); }

  void SplitLine(LineColumn) override { Notify(); }

  void FoldedLine(LineColumn) override { Notify(); }

  void Sorted() override { Notify(); }

  void AppendedToLine(LineColumn) override { Notify(); }

  void DeletedCharacters(LineColumn, ColumnNumberDelta) override { Notify(); }

  void SetCharacter(LineColumn) override { Notify(); }

  void InsertedCharacter(LineColumn) override { Notify(); }

  void Notify(bool update_disk_state = true) {
    std::optional<gc::Root<OpenBuffer>> root_this = buffer_.Lock();
    if (!root_this.has_value()) return;
    root_this->ptr()->work_queue()->Schedule(WorkQueue::Callback{
        .callback =
            gc::LockCallback(gc::BindFront(
                                 root_this->ptr()->editor().gc_pool(),
                                 [](gc::Root<OpenBuffer> buffer) {
                                   buffer->MaybeStartUpdatingSyntaxTrees();
                                 },
                                 buffer_)
                                 .ptr())});
    if (update_disk_state) {
      root_this->ptr()->SetDiskState(OpenBuffer::DiskState::kStale);
      if (root_this->ptr()->Read(buffer_variables::persist_state)) {
        switch (root_this->ptr()->backup_state_) {
          case OpenBuffer::DiskState::kCurrent: {
            root_this->ptr()->backup_state_ = OpenBuffer::DiskState::kStale;
            auto flush_backup_time = Now();
            flush_backup_time.tv_sec += 30;
            root_this->ptr()->work_queue()->Schedule(WorkQueue::Callback{
                .time = flush_backup_time,
                .callback = gc::LockCallback(
                    gc::BindFront(
                        root_this->ptr()->editor().gc_pool(),
                        [](gc::Root<OpenBuffer> locked_root_this) {
                          locked_root_this->UpdateBackup();
                        },
                        root_this->ptr().ToWeakPtr())
                        .ptr())});
          } break;

          case OpenBuffer::DiskState::kStale:
            break;  // Nothing.
        }
      }
    }

    root_this->ptr()->UpdateLastAction();
  }

 private:
  gc::WeakPtr<OpenBuffer> buffer_;
};

OpenBuffer::OpenBuffer(ConstructorAccessTag, Options options,
                       gc::Ptr<MapModeCommands> default_commands,
                       gc::Ptr<InputReceiver> mode,
                       NonNull<std::shared_ptr<Status>> status,
                       gc::Ptr<ExecutionContext> execution_context)
    : options_(std::move(options)),
      transformation_adapter_(
          MakeNonNullUnique<TransformationInputAdapterImpl>(*this)),
      contents_(MakeNonNullShared<DelegatingMutableLineSequenceObserver>(
          std::vector<NonNull<std::shared_ptr<MutableLineSequenceObserver>>>(
              {contents_observer_,
               cursors_tracker_.NewMutableLineSequenceObserver()}))),
      default_commands_(std::move(default_commands)),
      mode_(std::move(mode)),
      status_(std::move(status)),
      file_adapter_(
          MakeNonNullUnique<RegularFileAdapter>(RegularFileAdapter::Options{
              .thread_pool = editor().thread_pool(), .insert_lines = nullptr})),
      load_visual_state_(LazyValue<bool>{[this] {
        LOG(INFO) << "Loading visual state: " << Read(buffer_variables::name);
        std::visit(
            overload{
                IgnoreErrors{},
                [&](Path buffer_path) {
                  for (const auto& dir : options_.editor.edge_path()) {
                    Path state_path = Path::Join(
                        Path::Join(dir, EditorState::StatePathComponent()),
                        Path::Join(buffer_path,
                                   PathComponent::FromString(L".edge_state")));
                    file_system_driver()
                        ->Stat(state_path)
                        .Transform(
                            [state_path,
                             execution_context =
                                 execution_context_.ToWeakPtr()](struct stat) {
                              return VisitOptional(
                                  [&](gc::Root<ExecutionContext> context) {
                                    return context->EvaluateFile(state_path);
                                  },
                                  [] {
                                    return futures::Past(
                                        ValueOrError<gc::Root<Value>>(
                                            Error{LazyString{
                                                L"Buffer has been deleted."}}));
                                  },
                                  execution_context.Lock());
                            });
                  }
                }},
            Path::New(Read(buffer_variables::path)));
        auto paths = editor().edge_path();
        futures::ForEach(
            paths.begin(), paths.end(),
            [context = execution_context_](Path dir) {
              return context
                  ->EvaluateFile(Path::Join(
                      dir, ValueOrDie(Path::New(
                               LazyString{L"hooks/buffer-first-enter.cc"}))))
                  .Transform(
                      [](gc::Root<Value>)
                          -> futures::ValueOrError<IterationControlCommand> {
                        return Past(IterationControlCommand::kContinue);
                      })
                  .ConsumeErrors([](Error) {
                    return Past(IterationControlCommand::kContinue);
                  });
            });
        return true;
      }}),
      execution_context_(std::move(execution_context)) {
  work_queue()->OnSchedule().Add(std::bind_front(
      MaybeScheduleNextWorkQueueExecution,
      std::weak_ptr<WorkQueue>(work_queue().get_shared()),
      editor().work_queue(),
      NonNull<std::shared_ptr<std::optional<struct timespec>>>()));
  for (auto* v :
       {buffer_variables::symbol_characters, buffer_variables::tree_parser,
        buffer_variables::language_keywords, buffer_variables::typos,
        buffer_variables::identifier_behavior, buffer_variables::dictionary})
    variables_.string_variables.ObserveValue(v).Add([this] {
      UpdateTreeParser();
      return Observers::State::kAlive;
    });

  line_processor_map_.Add(LineProcessorKey{SingleLine{}},
                          [this](LineProcessorInput input) {
                            return LineMetadataCompilation(*this, input);
                          });
}

OpenBuffer::~OpenBuffer() { LOG(INFO) << "Start destructor: " << name(); }

EditorState& OpenBuffer::editor() const { return options_.editor; }

Status& OpenBuffer::status() const { return status_.value(); }

PossibleError OpenBuffer::IsUnableToPrepareToClose() const {
  if (options_.editor.modifiers().strength > Modifiers::Strength::kNormal) {
    return Success();
  }
  if (child_pid_.has_value() && !Read(buffer_variables::term_on_close))
    return Error{LazyString{L"Running subprocess "} +
                 Parenthesize(LazyString{L"pid: "} +
                              LazyString{std::to_wstring(child_pid_->read())})};
  return Success();
}

futures::ValueOrError<OpenBuffer::PrepareToCloseOutput>
OpenBuffer::PrepareToClose() {
  log_->Append(LazyString{L"PrepareToClose"});
  LOG(INFO) << "Preparing to close: " << Read(buffer_variables::name);
  return std::visit(
      overload{
          [&](Error error) -> futures::ValueOrError<PrepareToCloseOutput> {
            LOG(INFO) << name() << ": Unable to close: " << error;
            return futures::Past(error);
          },
          [&](EmptyValue) {
            return (options_.editor.modifiers().strength ==
                            Modifiers::Strength::kNormal
                        ? PersistState()
                        : futures::IgnoreErrors(PersistState()))
                .Transform([root_this = NewRoot()](EmptyValue)
                               -> futures::ValueOrError<PrepareToCloseOutput> {
                  LOG(INFO) << root_this->name() << ": State persisted.";
                  if (root_this->child_pid_.has_value()) {
                    if (root_this->Read(buffer_variables::term_on_close)) {
                      if (root_this->on_exit_handler_.has_value()) {
                        return futures::Past(Error{
                            LazyString{L"Already waiting for termination."}});
                      }
                      LOG(INFO) << "Sending termination and preparing handler: "
                                << root_this->Read(buffer_variables::name);
                      root_this->file_system_driver()->Kill(
                          root_this->child_pid_.value(), UnixSignal{SIGHUP});
                      auto future =
                          futures::Future<ValueOrError<PrepareToCloseOutput>>();
                      root_this->on_exit_handler_ =
                          [root_this,
                           consumer = std::move(future.consumer)]() mutable {
                            CHECK(!root_this->child_pid_.has_value());
                            LOG(INFO)
                                << "Subprocess terminated: "
                                << root_this->Read(buffer_variables::name);
                            root_this->PrepareToClose().SetConsumer(
                                std::move(consumer));
                          };
                      return std::move(future.value);
                    }
                    CHECK(root_this->options_.editor.modifiers().strength >
                          Modifiers::Strength::kNormal);
                  }
                  if (!root_this->dirty() ||
                      root_this->Read(buffer_variables::allow_dirty_delete))
                    return futures::Past(PrepareToCloseOutput{});

                  LOG(INFO)
                      << root_this->name() << ": attempting to save buffer.";
                  if (root_this->Read(buffer_variables::save_on_close))
                    return root_this->Save(Options::SaveType::kMainFile)
                        .Transform([root_this](EmptyValue) {
                          LOG(INFO) << "Buffer saved" << root_this->name();
                          return Success(PrepareToCloseOutput{});
                        });

                  return root_this->Save(Options::SaveType::kBackup)
                      .Transform([root_this](EmptyValue) {
                        LOG(INFO) << "Backup saved" << root_this->name();
                        return Success(PrepareToCloseOutput{
                            .dirty_contents_saved_to_backup = true});
                      });
                });
          }},
      IsUnableToPrepareToClose());
}

void OpenBuffer::Close() {
  log_->Append(LazyString{L"Closing"});
  if (dirty() && !Read(buffer_variables::allow_dirty_delete)) {
    if (Read(buffer_variables::save_on_close)) {
      log_->Append(LazyString{L"Saving buffer: "} +
                   Read(buffer_variables::name));
      Save(Options::SaveType::kMainFile);
    } else {
      log_->Append(LazyString{L"Saving backup: "} +
                   Read(buffer_variables::name));
      Save(Options::SaveType::kBackup);
    }
  }
  editor().line_marks().RemoveSource(name());
  LOG(INFO) << name() << ": Notify close observers";
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

void OpenBuffer::HandleDisplay() const { CHECK(load_visual_state_.get()); }

void OpenBuffer::Visit() {
  if (Read(buffer_variables::reload_on_enter)) {
    Reload();
    CheckPosition();
  }
  UpdateLastAction();
  last_visit_ = last_action_;
  if (options_.handle_visit != nullptr) {
    options_.handle_visit(*this);
  }
}

struct timespec OpenBuffer::last_visit() const { return last_visit_; }
struct timespec OpenBuffer::last_action() const { return last_action_; }

futures::Value<PossibleError> OpenBuffer::PersistState() const {
  auto trace = log_->NewChild(LazyString{L"Persist State"});
  if (!Read(buffer_variables::persist_state) ||
      !load_visual_state_.has_value()) {
    return futures::Past(Success());
  }

  return OnError(GetEdgeStateDirectory(),
                 [weak_status = std::weak_ptr<Status>(status_.get_shared())](
                     Error error) {
                   error = AugmentError(
                       LazyString{L"Unable to get Edge state directory"},
                       std::move(error));
                   VisitPointer(
                       weak_status,
                       [&error](NonNull<std::shared_ptr<Status>> status) {
                         status->Set(error);
                       },
                       [] {});
                   return futures::Past(error);
                 })
      .Transform([serialized_state = SerializeState(position(), variables_),
                  file_system_driver = execution_context_->file_system_driver(),
                  &editor = editor(),
                  weak_status = std::weak_ptr<Status>(status_.get_shared())](
                     Path edge_state_directory) {
        Path path = Path::Join(edge_state_directory,
                               PathComponent::FromString(L".edge_state"));
        LOG(INFO) << "PersistState: Preparing state file: " << path;
        LineSequence header = LineSequence::WithLine(Line{
                                  SINGLE_LINE_CONSTANT(L"// State of file: ") +
                                  EscapedString::FromString(path.read())
                                      .EscapedRepresentation()}) +
                              LineSequence{};
        return futures::OnError(
            SaveContentsToFile(path, header + serialized_state,
                               editor.thread_pool(),
                               file_system_driver.value()),
            [weak_status](Error error) {
              error = AugmentError(LazyString{L"Unable to persist state"},
                                   std::move(error));
              VisitPointer(
                  weak_status,
                  [&error](NonNull<std::shared_ptr<Status>> status) {
                    status->Set(error);
                  },
                  [] {});
              return futures::Past(error);
            });
      });
}

void OpenBuffer::ClearContents() {
  VLOG(5) << "Clear contents of buffer: " << Read(buffer_variables::name);
  options_.editor.line_marks().RemoveExpiredMarksFromSource(name());
  options_.editor.line_marks().ExpireMarksFromSource(contents().snapshot(),
                                                     name());
  contents_.EraseLines(LineNumber(0), LineNumber(0) + contents_.size(),
                       MutableLineSequence::ObserverBehavior::kHide);
  file_adapter_->SetPositionToZero();
  undo_state_.Clear();
}

void OpenBuffer::AppendEmptyLine() {
  auto follower = GetEndPositionFollower();
  contents_.push_back(Line());
}

void OpenBuffer::SignalEndOfFile() {
  UpdateLastAction();
  CHECK(fd() == nullptr);
  CHECK(fd_error() == nullptr);
  // We can remove expired marks now. We know that the set of fresh marks
  // is now complete.
  editor().line_marks().RemoveExpiredMarksFromSource(name());

  end_of_file_observers_.Notify();
  contents_observer_->Notify(false);

  if (Read(buffer_variables::reload_after_exit)) {
    Set(buffer_variables::reload_after_exit,
        Read(buffer_variables::default_reload_after_exit));
    Reload();
  }

  if (Read(buffer_variables::close_after_clean_exit) &&
      child_exit_status_.has_value() && WIFEXITED(child_exit_status_.value()) &&
      WEXITSTATUS(child_exit_status_.value()) == 0)
    editor().CloseBuffer(*this);

  if (std::optional<gc::Root<OpenBuffer>> current_buffer =
          editor().current_buffer();
      current_buffer.has_value() && name() == BufferName{BufferListId{}})
    current_buffer->ptr()->Reload();
}

void OpenBuffer::SendEndOfFileToProcess() {
  if (fd() == nullptr) {
    status().SetInformationText(Line{
        SINGLE_LINE_CONSTANT(L"No active subprocess for current buffer.")});
    return;
  }
  if (Read(buffer_variables::pts)) {
    char str[1] = {4};
    if (write(fd()->fd().read(), str, sizeof(str)) == -1) {
      status().SetInformationText(LineBuilder{
          SINGLE_LINE_CONSTANT(L"Sending EOF failed: ") +
          SingleLine{LazyString{FromByteString(
              strerror(errno))}}}.Build());
      return;
    }
    status().SetInformationText(Line{SINGLE_LINE_CONSTANT(L"EOF sent")});
  } else {
    if (shutdown(fd()->fd().read(), SHUT_WR) == -1) {
      status().SetInformationText(LineBuilder{
          SINGLE_LINE_CONSTANT(L"shutdown(SHUT_WR) failed: ") +
          SingleLine{LazyString{FromByteString(
              strerror(errno))}}}.Build());
      return;
    }
    status().SetInformationText(Line{SINGLE_LINE_CONSTANT(L"shutdown sent")});
  }
}

std::unique_ptr<bool, std::function<void(bool*)>>
OpenBuffer::GetEndPositionFollower() {
  if (!Read(buffer_variables::follow_end_of_file)) return nullptr;
  if (position() < end_position() && file_adapter_->position() == std::nullopt)
    return nullptr;  // Not at the end, so user must have scrolled up.
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this](bool* value) {
        delete value;
        set_position(file_adapter_->position().value_or(end_position()));
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

void OpenBuffer::UpdateTreeParser() {
  if (!ptr_this_.has_value()) return;
  futures::Past(Path::New(Read(buffer_variables::dictionary)))
      .Transform([&](Path dictionary_path) {
        return OpenFileIfFound(OpenFileOptions{
            .editor_state = editor(),
            .path = dictionary_path,
            .insertion_type = BuffersList::AddBufferType::kIgnore,
            .use_search_paths = false});
      })
      .Transform([](gc::Root<OpenBuffer> dictionary_root) {
        return dictionary_root->WaitForEndOfFile().Transform(
            [dictionary_root](EmptyValue) {
              return dictionary_root->editor().thread_pool().Run(
                  [contents = dictionary_root->contents().snapshot()] {
                    return Success(SortedLineSequence(contents));
                  });
            });
      })
      .ConsumeErrors([](Error) {
        return futures::Past(SortedLineSequence(LineSequence()));
      })
      .Transform([root_this = NewRoot()](SortedLineSequence dictionary) {
        root_this->buffer_syntax_parser_.UpdateParser(
            BufferSyntaxParser::ParserOptions{
                .parser_name = OptionalFrom(
                    ParserId::New(NonEmptySingleLine::New(SingleLine::New(
                        root_this->Read(buffer_variables::tree_parser))))),
                .typos_set = MaterializeUnorderedSet(
                    TokenizeBySpaces(
                        LineSequence::BreakLines(
                            root_this->Read(buffer_variables::typos))
                            .FoldLines()) |
                    std::views::transform(&Token::value)),
                .language_keywords = MaterializeUnorderedSet(
                    TokenizeBySpaces(
                        LineSequence::BreakLines(
                            root_this->Read(
                                buffer_variables::language_keywords))
                            .FoldLines()) |
                    std::views::transform(&Token::value)),
                .symbol_characters =
                    root_this->Read(buffer_variables::symbol_characters),
                .identifier_behavior =
                    root_this->Read(buffer_variables::identifier_behavior) ==
                            LazyString{L"color-by-hash"}
                        ? IdentifierBehavior::kColorByHash
                        : IdentifierBehavior::kNone,
                .dictionary = std::move(dictionary)});
        root_this->MaybeStartUpdatingSyntaxTrees();
        return EmptyValue();
      });
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
  buffer_syntax_parser_.ObserveTrees().Add(WeakPtrLockingObserver(
      [](OpenBuffer& buffer) {
        // Trigger a wake up alarm.
        buffer.work_queue()->Wait(Now());
      },
      weak_this));

  UpdateTreeParser();

  gc::Root<OpenBuffer> root = NewRoot();
  execution_context_->environment()->Define(
      Identifier{NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"buffer")}},
      VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(editor().gc_pool(), root));

  execution_context_->environment()->Define(
      Identifier{NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"sleep")}},
      vm::NewCallback(editor().gc_pool(),
                      PurityType{.reads_external_inputs = true},
                      [weak_this](double delay_seconds) {
                        return VisitPointer(
                            weak_this.Lock(),
                            [delay_seconds](gc::Root<OpenBuffer> root_this) {
                              return root_this->work_queue()->Wait(
                                  AddSeconds(Now(), delay_seconds));
                            },
                            [&] { return futures::Past(EmptyValue()); });
                      }));

  Set(buffer_variables::name, ToSingleLine(options_.name).read().read());
  if (options_.path.has_value())
    Set(buffer_variables::path, options_.path.value().read());
  Set(buffer_variables::pts_path, LazyString{});
  Set(buffer_variables::command, LazyString{});
  Set(buffer_variables::reload_after_exit, false);
  if (std::holds_alternative<PasteBuffer>(name()) ||
      std::holds_alternative<FuturePasteBuffer>(name())) {
    Set(buffer_variables::allow_dirty_delete, true);
    Set(buffer_variables::show_in_buffers_list, false);
    Set(buffer_variables::delete_into_paste_buffer, false);
  }
  ClearContents();

  contents_observer_->SetOpenBuffer(weak_this);
}

void OpenBuffer::MaybeStartUpdatingSyntaxTrees() {
  buffer_syntax_parser_.Parse(contents_.snapshot());
}

void OpenBuffer::StartNewLine(Line line) {
  TRACK_OPERATION(OpenBuffer_StartNewLine);
  AppendLines({std::move(line)});
}

void OpenBuffer::AppendLines(
    std::vector<Line> lines,
    language::text::MutableLineSequence::ObserverBehavior observer_behavior) {
  TRACK_OPERATION(OpenBuffer_AppendLines);

  auto lines_added = LineNumberDelta(lines.size());
  if (lines_added.IsZero()) return;
  lines_read_rate_.IncrementAndGetEventsPerSecond(lines_added.read());
  LineNumberDelta start_new_section = contents_.size() - LineNumberDelta(1);
  contents_.append_back(
      UpdateLineMetadata(*this, line_processor_map_, std::move(lines)),
      observer_behavior);
  if (Read(buffer_variables::contains_line_marks)) {
    TRACK_OPERATION(OpenBuffer_StartNewLine_ScanForMarks);
    ResolvePathOptions<EmptyValue>::New(
        editor(), MakeNonNullShared<FileSystemDriver>(editor().thread_pool()))
        .Transform(
            [buffer_name = name(), lines_added, contents = contents_.snapshot(),
             start_new_section,
             &editor = editor()](ResolvePathOptions<EmptyValue> options) {
              for (LineNumberDelta i; i < lines_added; ++i) {
                auto source_line = LineNumber() + start_new_section + i;
                options.path = ToLazyString(contents.at(source_line));
                ResolvePath(options).Transform(
                    [&editor, buffer_name,
                     source_line](ResolvePathOutput<EmptyValue> results) {
                      LineMarks::Mark mark{
                          .source_buffer = buffer_name,
                          .source_line = source_line,
                          .target_buffer = BufferFileId(results.path),
                          .target_line_column =
                              results.position.value_or(LineColumn())};
                      LOG(INFO) << "Found a mark: " << mark;
                      editor.line_marks().AddMark(std::move(mark));
                      return Success();
                    });
              }
              return Success();
            });
  }
}

futures::Value<PossibleError> OpenBuffer::Reload() {
  LOG(INFO) << name() << ": Reload starts.";
  display_data_ = MakeNonNullUnique<BufferDisplayData>();

  if (child_pid_.has_value()) {
    LOG(INFO) << "Sending SIGHUP.";
    file_system_driver()->Kill(ProcessId(-child_pid_->read()),
                               UnixSignal(SIGHUP));
    Set(buffer_variables::reload_after_exit, true);
    return futures::Past(Success());
  }

  switch (reload_state_) {
    case ReloadState::kDone:
      reload_state_ = ReloadState::kOngoing;
      break;
    case ReloadState::kOngoing:
      reload_state_ = ReloadState::kPending;
      return futures::Past(
          Error{LazyString{L"Reload is already in progress."}});
    case ReloadState::kPending:
      return futures::Past(Error{
          LazyString{L"Reload is already in progress and new one scheduled."}});
  }

  return std::visit(
             overload{
                 [this](
                     ExecutionContext::CompilationResult compilation_result) {
                   if (editor().exit_value().has_value())
                     return futures::Past(Success());
                   LOG(INFO)
                       << "Starting reload: " << Read(buffer_variables::name);
                   return futures::IgnoreErrors(
                       compilation_result.evaluate().Transform(
                           [](gc::Root<vm::Value>) -> PossibleError {
                             return Success();
                           }));
                 },
                 [](Error error) {
                   LOG(INFO) << "OnReload handler: " << error;
                   return futures::Past(Success());
                 }},
             execution_context()->FunctionCall(
                 IDENTIFIER_CONSTANT(L"OnReload"),
                 {VMTypeMapper<gc::Ptr<OpenBuffer>>::New(ptr_this_->pool(),
                                                         ptr_this_.value())
                      .ptr()}))
      .Transform([this](EmptyValue) {
        if (Read(buffer_variables::clear_on_reload)) {
          ClearContents();
          SetDiskState(DiskState::kCurrent);
        }
        return options_.generate_contents != nullptr
                   ? futures::IgnoreErrors(options_.generate_contents(*this))
                   : futures::Past(Success());
      })
      .Transform([this](EmptyValue) {
        return futures::OnError(
            GetEdgeStateDirectory().Transform(options_.log_supplier),
            [](Error error) {
              LOG(INFO) << "Error opening log: " << error;
              return futures::Past(NewNullLog());
            });
      })
      .Transform(
          [root_this = ptr_this_->ToRoot()](NonNull<std::unique_ptr<Log>> log) {
            root_this->log_ = std::move(log);
            switch (root_this->reload_state_) {
              case ReloadState::kDone:
                LOG(FATAL) << "Invalid reload state! Can't be kDone.";
                break;
              case ReloadState::kOngoing:
                root_this->reload_state_ = ReloadState::kDone;
                break;
              case ReloadState::kPending:
                root_this->reload_state_ = ReloadState::kDone;
                root_this->SignalEndOfFile();
                return root_this->Reload();
            }
            LOG(INFO) << "Reload finished evaluation: " << root_this->name();
            root_this->SignalEndOfFile();
            return futures::Past(Success());
          });
}

futures::Value<PossibleError> OpenBuffer::Save(Options::SaveType save_type) {
  LOG(INFO) << "Saving buffer: " << Read(buffer_variables::name);
  if (options_.handle_save == nullptr) {
    status_->InsertError(Error{LazyString{L"Buffer can't be saved."}});
    return futures::Past(
        PossibleError(Error{LazyString{L"Buffer can't be saved."}}));
  }
  LineSequence contents_snapshot = contents().snapshot();
  futures::Value<PossibleError> output = options_.handle_save(
      Options::HandleSaveOptions{.buffer = NewRoot(), .save_type = save_type});
  if (save_type == OpenBuffer::Options::SaveType::kMainFile)
    output = std::move(output).Transform([&editor = editor(), contents_snapshot,
                                          root_buffer = NewRoot()](EmptyValue)
                                             -> futures::Value<PossibleError> {
      if (contents_snapshot == root_buffer->contents().snapshot())
        root_buffer->SetDiskState(OpenBuffer::DiskState::kCurrent);
      if (root_buffer->Read(buffer_variables::trigger_reload_on_buffer_write)) {
        std::ranges::for_each(
            editor.buffer_registry().buffers() | gc::view::Value |
                std::views::filter([](OpenBuffer& reload_buffer) {
                  return reload_buffer.Read(
                      buffer_variables::reload_on_buffer_write);
                }),
            [&](OpenBuffer& reload_buffer) {
              LOG(INFO) << "Write of " << root_buffer->name()
                        << " triggers reload: "
                        << reload_buffer.Read(buffer_variables::name);
              reload_buffer.Reload();
            });
      }

      return futures::Past(Success());
    });
  return OnError(
      std::move(output),
      [weak_status = std::weak_ptr<Status>(status_.get_shared())](Error error) {
        VisitPointer(
            weak_status,
            [&error](NonNull<std::shared_ptr<Status>> status) {
              status->Set(AugmentError(LazyString{L"🖫 Save failed"}, error));
            },
            [] {});
        return futures::Past(error);
      });
}

futures::ValueOrError<Path> OpenBuffer::GetEdgeStateDirectory() const {
  auto path_vector = editor().edge_path();
  if (path_vector.empty())
    return futures::Past(
        ValueOrError<Path>(Error{LazyString{L"Empty edge path."}}));
  FUTURES_ASSIGN_OR_RETURN(
      Path file_path,
      AugmentError(
          LazyString{L"Unable to persist buffer with invalid path "} +
              Parenthesize(dirty() ? LazyString{L"dirty"}
                                   : LazyString{L"clean"}) +
              LazyString{L" "} +
              (disk_state_ == DiskState::kStale ? LazyString{L"modified"}
                                                : LazyString{L"not modified"}),
          AbsolutePath::New(Read(buffer_variables::path))));

  if (file_path.GetRootType() != Path::RootType::kAbsolute) {
    return futures::Past(ValueOrError<Path>(
        Error{LazyString{L"Unable to persist buffer without absolute path: "} +
              file_path.read()}));
  }

  FUTURES_ASSIGN_OR_RETURN(std::list<PathComponent> file_path_components,
                           AugmentError(LazyString{L"Unable to split path"},
                                        file_path.DirectorySplit()));

  file_path_components.push_front(EditorState::StatePathComponent());

  auto path = std::make_shared<Path>(path_vector[0]);
  auto error = std::make_shared<std::optional<Error>>();
  LOG(INFO) << "GetEdgeStateDirectory: Preparing state directory: " << *path;
  return futures::ForEachWithCopy(
             file_path_components.begin(), file_path_components.end(),
             [file_system_driver = file_system_driver(), path,
              error](auto component) {
               *path = Path::Join(*path, component);
               return file_system_driver->Stat(*path)
                   .Transform([path, error](struct stat stat_buffer) {
                     if (S_ISDIR(stat_buffer.st_mode)) {
                       return Success(IterationControlCommand::kContinue);
                     }
                     *error = Error{
                         LazyString{L"Oops, exists, but is not a directory: "} +
                         path->read()};
                     return Success(IterationControlCommand::kStop);
                   })
                   .ConsumeErrors([file_system_driver, path, error](Error) {
                     return file_system_driver->Mkdir(*path, 0700)
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
  log_->Append(LazyString{L"UpdateBackup starts."});
  if (options_.handle_save != nullptr) {
    options_.handle_save(Options::HandleSaveOptions{
        .buffer = NewRoot(), .save_type = Options::SaveType::kBackup});
  }
  backup_state_ = DiskState::kCurrent;
}

BufferDisplayData& OpenBuffer::display_data() { return display_data_.value(); }
const BufferDisplayData& OpenBuffer::display_data() const {
  return display_data_.value();
}

void OpenBuffer::SortContents(
    LineNumber start, LineNumberDelta length,
    std::function<bool(const Line&, const Line&)> compare) {
  CHECK_GE(length, LineNumberDelta());
  CHECK_LE((start + length).ToDelta(), lines_size());
  contents_.sort(start, length, compare);
}

void OpenBuffer::SortAllContents(
    std::function<bool(const Line&, const Line&)> compare) {
  CHECK_GT(lines_size(), LineNumberDelta());
  SortContents(LineNumber(), lines_size(), std::move(compare));
}

void OpenBuffer::SortAllContentsIgnoringCase() {
  SortAllContents([](const Line& a, const Line& b) {
    return LowerCase(a.contents()) < LowerCase(b.contents());
  });
}

LineNumberDelta OpenBuffer::lines_size() const { return contents_.size(); }

LineNumber OpenBuffer::EndLine() const { return contents_.EndLine(); }

InputReceiver& OpenBuffer::mode() const { return mode_.value(); }

gc::Root<InputReceiver> OpenBuffer::ResetMode() {
  gc::Root<InputReceiver> copy = mode_.ToRoot();
  mode_ = MapMode::New(editor().gc_pool(), default_commands_).ptr();
  return copy;
}

gc::Ptr<MapModeCommands> OpenBuffer::default_commands() {
  return default_commands_;
}

void OpenBuffer::EraseLines(LineNumber first, LineNumber last) {
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + contents_.size());
  contents_.EraseLines(first, last);
}

void OpenBuffer::InsertLine(LineNumber line_position, Line line) {
  contents_.insert_line(
      line_position,
      UpdateLineMetadata(*this, line_processor_map_, {std::move(line)})[0]);
}

void OpenBuffer::AppendLine(SingleLine str) {
  if (reading_from_parser_) {
    switch (str.get(ColumnNumber(0))) {
      case 'E':
        return AppendRawLine(str.Substring(ColumnNumber(1)));
    }
    return;
  }

  if (contents_.size() == LineNumberDelta(1) &&
      contents_.back().EndColumn().IsZero()) {
    if (str == LazyString{L"EDGE PARSER v1.0"}) {
      reading_from_parser_ = true;
      return;
    }
  }

  AppendRawLine(std::move(str));
}

void OpenBuffer::AppendRawLine(
    SingleLine str, MutableLineSequence::ObserverBehavior observer_behavior) {
  AppendRawLine(LineBuilder{std::move(str)}.Build(), observer_behavior);
}

void OpenBuffer::AppendRawLine(
    Line line, MutableLineSequence::ObserverBehavior observer_behavior) {
  auto follower = GetEndPositionFollower();
  contents_.append_back(
      UpdateLineMetadata(*this, line_processor_map_, {std::move(line)}),
      observer_behavior);
}

void OpenBuffer::AppendToLastLine(SingleLine str) {
  AppendToLastLine(LineBuilder{std::move(str)}.Build());
}

void OpenBuffer::AppendToLastLine(Line line) {
  TRACK_OPERATION(OpenBuffer_AppendToLastLine);
  auto follower = GetEndPositionFollower();
  LineBuilder options(contents_.back());
  options.Append(LineBuilder(std::move(line)));
  AppendRawLine(std::move(options).Build(),
                MutableLineSequence::ObserverBehavior::kHide);
  contents_.EraseLines(contents_.EndLine() - LineNumberDelta(1),
                       contents_.EndLine(),
                       MutableLineSequence::ObserverBehavior::kHide);
}

futures::ValueOrError<gc::Root<Value>> OpenBuffer::EvaluateExpression(
    const NonNull<std::shared_ptr<Expression>>& expr,
    gc::Root<Environment> environment) {
  // TODO(2025-05-26, trivial): Replace with a method from execution_context.
  return Evaluate(expr, editor().gc_pool(), environment,
                  [work_queue = work_queue(), root_this = ptr_this_->ToRoot()](
                      OnceOnlyFunction<void()> callback) {
                    work_queue->Schedule(
                        WorkQueue::Callback{.callback = std::move(callback)});
                  });
}

NonNull<std::shared_ptr<WorkQueue>> OpenBuffer::work_queue() const {
  return execution_context_->work_queue();
}

OpenBuffer::LockFunction OpenBuffer::GetLockFunction() {
  return [root_this = ptr_this_->ToRoot()](
             OnceOnlyFunction<void(OpenBuffer&)> callback) {
    root_this->work_queue()->Schedule(WorkQueue::Callback{
        .callback = [root_this, callback = std::move(callback)] mutable {
          std::move(callback)(root_this.ptr().value());
        }});
  };
}

void OpenBuffer::AddLineProcessor(
    language::text::LineProcessorKey key,
    std::function<
        language::ValueOrError<language::text::LineProcessorOutputFuture>(
            language::text::LineProcessorInput)>
        callback) {
  line_processor_map_.Add(key, callback);
}

void OpenBuffer::DeleteRange(const Range& range) {
  if (range.IsSingleLine()) {
    contents_.DeleteCharactersFromLine(
        range.begin(), range.end().column - range.begin().column);
    SetMutableLineSequenceLineMetadata(*this, line_processor_map_, contents_,
                                       range.begin().line);
  } else {
    contents_.DeleteToLineEnd(range.begin());
    contents_.DeleteCharactersFromLine(LineColumn(range.end().line),
                                       range.end().column.ToDelta());
    // Lines in the middle.
    EraseLines(range.begin().line + LineNumberDelta(1), range.end().line);
    contents_.FoldNextLine(range.begin().line);
    SetMutableLineSequenceLineMetadata(*this, line_processor_map_, contents_,
                                       range.begin().line);
  }
}

LineColumn OpenBuffer::InsertInPosition(
    const LineSequence& contents_to_insert, const LineColumn& input_position,
    const std::optional<LineModifierSet>& modifiers) {
  VLOG(5) << "InsertInPosition: " << input_position << " "
          << (modifiers.has_value() ? modifiers.value().size() : 1);
  auto blocker = cursors_tracker_.DelayTransformations();
  LineColumn position = input_position;
  if (position.line > contents_.EndLine()) {
    position.line = contents_.EndLine();
    position.column = contents_.at(position.line).EndColumn();
  }
  if (position.column > contents_.at(position.line).EndColumn()) {
    position.column = contents_.at(position.line).EndColumn();
  }
  contents_.SplitLine(position);
  contents_.insert(position.line.next(), contents_to_insert, modifiers);
  contents_.FoldNextLine(position.line);
  SetMutableLineSequenceLineMetadata(*this, line_processor_map_, contents_,
                                     position.line);

  LineNumber last_line =
      position.line + contents_to_insert.size() - LineNumberDelta(1);
  CHECK_LE(last_line, EndLine());
  auto line = LineAt(last_line);
  CHECK(line.has_value());
  ColumnNumber column = line->EndColumn();

  contents_.FoldNextLine(last_line);
  SetMutableLineSequenceLineMetadata(*this, line_processor_map_, contents_,
                                     last_line);
  return LineColumn(last_line, column);
}

void OpenBuffer::MaybeAdjustPositionCol() {
  VisitPointer(
      OptionalCurrentLine(),
      [&](Line line) {
        set_current_position_col(std::min(position().column, line.EndColumn()));
      },
      [] {});
}

void OpenBuffer::MaybeExtendLine(LineColumn position) {
  CHECK_LE(position.line, contents_.EndLine());
  const Line& line = contents_.at(position.line);
  if (line.EndColumn() > position.column + ColumnNumberDelta(1)) return;

  LineBuilder options(line);
  options.Append(LineBuilder{SingleLine{LazyString{
      position.column - line.EndColumn() + ColumnNumberDelta(1), L' '}}});
  contents_.set_line(position.line, std::move(options).Build());
}

void OpenBuffer::CheckPosition() {
  if (position().line > contents_.EndLine()) {
    set_position(LineColumn(contents_.EndLine()));
  }
}

CursorsSet& OpenBuffer::FindOrCreateCursors(const std::wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

const CursorsSet* OpenBuffer::FindCursors(const std::wstring& name) const {
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

void OpenBuffer::set_active_cursors(const std::vector<LineColumn>& positions) {
  if (positions.empty()) return;

  CursorsSet& cursors = active_cursors();
  FindOrCreateCursors(kOldCursors).swap(&cursors);
  cursors.clear();
  cursors.insert(positions.begin(), positions.end());

  // We find the first position (rather than just take cursors->begin()) so
  // that we start at the first requested position.
  cursors.SetCurrentCursor(positions.front());
}

void OpenBuffer::ToggleActiveCursors() {
  LineColumn desired_position = position();

  CursorsSet& cursors = active_cursors();
  FindOrCreateCursors(kOldCursors).swap(&cursors);

  // TODO: Maybe it'd be best to pick the nearest after the cursor?
  // TODO: This should probably be merged somewhat with set_active_cursors?
  if (std::ranges::any_of(cursors, [&desired_position](auto& c) {
        return c == desired_position;
      })) {
    LOG(INFO) << "Desired position " << desired_position << " prevails.";
    cursors.SetCurrentCursor(desired_position);
    CHECK_LE(position().line, LineNumber(0) + lines_size());
    return;
  }

  cursors.SetCurrentCursor(*cursors.begin());
  LOG(INFO) << "Picked up the first cursor: " << position();
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::PushActiveCursors() {
  auto stack_size = cursors_tracker_.Push();
  status_->SetInformationText(
      LineBuilder{SINGLE_LINE_CONSTANT(L"cursors stack ") +
                  Parenthesize(NonEmptySingleLine{stack_size}) +
                  SINGLE_LINE_CONSTANT(L": +")}
          .Build());
}

void OpenBuffer::PopActiveCursors() {
  auto stack_size = cursors_tracker_.Pop();
  if (stack_size == 0) {
    status_->InsertError(
        Error{LazyString{L"cursors stack: -: Stack is empty!"}});
    return;
  }
  status_->SetInformationText(
      LineBuilder{SINGLE_LINE_CONSTANT(L"cursors stack ") +
                  Parenthesize(NonEmptySingleLine(stack_size - 1)) +
                  SINGLE_LINE_CONSTANT(L": -")}
          .Build());
}

void OpenBuffer::SetActiveCursorsToMarks() {
  // To avoid repetitions, insert them first into a set.
  const std::set<LineColumn> cursors =
      std::invoke([&]() -> std::set<LineColumn> {
        if (const std::set<LineColumn> marks = GetSetWithKeys(GetLineMarks());
            !marks.empty())
          return marks;
        else if (const std::set<LineColumn> expired_marks =
                     GetSetWithKeys(GetExpiredLineMarks());
                 !expired_marks.empty())
          return expired_marks;
        else
          return {};
      });
  if (cursors.empty())
    status_->InsertError(Error{LazyString{L"Buffer has no marks!"}});
  else
    set_active_cursors(std::vector<LineColumn>(cursors.begin(), cursors.end()));
}

void OpenBuffer::set_current_cursor(LineColumn new_value) {
  CursorsSet& cursors = active_cursors();
  // Need to do find + erase because cursors is a multiset; we only want to
  // erase one cursor, rather than all cursors with the current value.
  cursors.erase(position());
  cursors.insert(new_value);
  cursors.SetCurrentCursor(new_value);
}

SeekInput OpenBuffer::NewSeekInput(Structure structure, Direction direction,
                                   LineColumn* position) const {
  return SeekInput{
      .contents = contents().snapshot(),
      .structure = structure,
      .direction = direction,
      .line_prefix_characters = MaterializeUnorderedSet(
          Read(buffer_variables::line_prefix_characters)),
      .symbol_characters =
          MaterializeUnorderedSet(Read(buffer_variables::symbol_characters)),
      .parse_tree = parse_tree(),
      .cursors = FindCursors(L""),
      .position = position,
  };
}

void OpenBuffer::CreateCursor() {
  if (options_.editor.modifiers().structure == Structure::kChar) {
    CHECK_LE(position().line, LineNumber(0) + contents_.size());
    active_cursors().insert(position());
  } else {
    auto structure = options_.editor.modifiers().structure;
    Modifiers tmp_modifiers = options_.editor.modifiers();
    tmp_modifiers.structure = Structure::kCursor;
    Range range = FindPartialRange(tmp_modifiers, position());
    if (range.empty()) return;
    options_.editor.set_direction(Direction::kForwards);
    LOG(INFO) << "Range for cursors: " << range;
    while (!range.empty()) {
      LineColumn tmp_first = range.begin();
      SeekToNext(NewSeekInput(structure, Direction::kForwards, &tmp_first));
      if (tmp_first > range.begin() && tmp_first < range.end()) {
        VLOG(5) << "Creating cursor at: " << tmp_first;
        active_cursors().insert(tmp_first);
      }
      if (!SeekToLimit(
              NewSeekInput(structure, Direction::kForwards, &tmp_first))) {
        break;
      }
      range.set_begin(tmp_first);
    }
  }
  status_->SetInformationText(Line{SINGLE_LINE_CONSTANT(L"Cursor created.")});
}

LineColumn OpenBuffer::FindNextCursor(LineColumn position,
                                      const Modifiers& modifiers) {
  LOG(INFO) << "Visiting next cursor: " << modifiers;
  Direction direction = modifiers.direction;
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
  size_t repetitions = std::min(
      options_.editor.modifiers().repetitions.value_or(1), cursors.size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors.DeleteCurrentCursor();
  }
  CheckPosition();
}

void OpenBuffer::DestroyOtherCursors() {
  CheckPosition();
  LineColumn old_position = position();
  CHECK_LE(old_position, LineColumn(LineNumber(0) + contents_.size()));
  CursorsSet& cursors = active_cursors();
  cursors.clear();
  cursors.insert(old_position);
  Set(buffer_variables::multiple_cursors, false);
}

Range OpenBuffer::FindPartialRange(const Modifiers& modifiers,
                                   LineColumn position) const {
  const auto forward = modifiers.direction;
  const auto backward = ReverseDirection(forward);

  position.line = std::min(contents_.EndLine(), position.line);
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
      modifiers.structure != Structure::kTree) {
    // TODO: Handle this in structure.
    Seek(contents_.snapshot(), &position).Backwards().WrappingLines().Once();
  }

  LineColumn output_begin = position;
  LOG(INFO) << "Initial position: " << position
            << ", structure: " << modifiers.structure;
  if (GetStructureSpaceBehavior(modifiers.structure) ==
      StructureSpaceBehavior::kForwards) {
    SeekToNext(NewSeekInput(modifiers.structure, forward, &output_begin));
  }

  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      output_begin = modifiers.direction == Direction::kForwards
                         ? std::max(position, output_begin)
                         : std::min(position, output_begin);
      break;

    case Modifiers::LIMIT_CURRENT: {
      if (SeekToLimit(
              NewSeekInput(modifiers.structure, backward, &output_begin))) {
        Seek(contents_.snapshot(), &output_begin)
            .WrappingLines()
            .WithDirection(forward)
            .Once();
      }
    } break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (SeekToLimit(
              NewSeekInput(modifiers.structure, backward, &output_begin))) {
        SeekToNext(NewSeekInput(modifiers.structure, backward, &output_begin));
        SeekToLimit(NewSeekInput(modifiers.structure, forward, &output_begin));
      }
  }

  LOG(INFO) << "After seek, initial position: " << output_begin;
  LineColumn output_end = modifiers.direction == Direction::kForwards
                              ? std::max(position, output_begin)
                              : std::min(position, output_begin);
  bool move_start = true;
  for (size_t i = 1; i < modifiers.repetitions.value_or(1); i++) {
    LineColumn start_position = output_end;
    if (!SeekToLimit(NewSeekInput(modifiers.structure, forward, &output_end))) {
      move_start = false;
      break;
    }
    SeekToNext(NewSeekInput(modifiers.structure, forward, &output_end));
    if (output_end == start_position) {
      break;
    }
  }

  LOG(INFO) << "After repetitions: " << output_begin << " to " << output_end;
  switch (modifiers.boundary_end) {
    case Modifiers::CURRENT_POSITION:
      break;

    case Modifiers::LIMIT_CURRENT:
      move_start &=
          SeekToLimit(NewSeekInput(modifiers.structure, forward, &output_end));
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      move_start &=
          SeekToLimit(NewSeekInput(modifiers.structure, forward, &output_end));
      SeekToNext(NewSeekInput(modifiers.structure, forward, &output_end));
  }
  LOG(INFO) << "After adjusting end: " << output_begin << " to " << output_end;

  if (output_begin > output_end) {
    CHECK(modifiers.direction == Direction::kBackwards);
    std::swap(output_begin, output_end);
    if (move_start) {
      Seek(contents_.snapshot(), &output_begin).WrappingLines().Once();
    }
  }
  LOG(INFO) << "After wrap: " << output_begin << " to " << output_end;
  return Range(output_begin, output_end);
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

const VisualOverlayMap& OpenBuffer::visual_overlay_map() const {
  return visual_overlay_map_;
}

VisualOverlayMap OpenBuffer::SetVisualOverlayMap(VisualOverlayMap value) {
  VisualOverlayMap previous_value = std::move(visual_overlay_map_);
  visual_overlay_map_ = std::move(value);
  return previous_value;
}

NonNull<std::unique_ptr<TerminalAdapter>> OpenBuffer::NewTerminal() {
  class Adapter : public TerminalAdapter::Receiver {
   public:
    Adapter(OpenBuffer& buffer) : buffer_(buffer) {}

    void EraseLines(LineNumber first, LineNumber last) override {
      buffer_.EraseLines(first, last);
    }

    void AppendEmptyLine() override { buffer_.AppendEmptyLine(); }

    infrastructure::TerminalName name() override {
      return infrastructure::TerminalName{LazyString{L"Terminal:"} +
                                          ToSingleLine(buffer_.name()).read()};
    }

    std::optional<infrastructure::FileDescriptor> fd() override {
      if (const FileDescriptorReader* fd = buffer_.fd(); fd != nullptr)
        return fd->fd();
      return std::nullopt;
    }

    language::ObservableValue<LineColumnDelta>& view_size() override {
      return buffer_.display_data().view_size();
    }

    void Bell() override {
      buffer_.status().Bell();
      audio::BeepFrequencies(
          buffer_.editor().audio_player(), 0.1,
          {audio::Frequency(783.99), audio::Frequency(523.25),
           audio::Frequency(659.25)});
    }

    void Warn(Error error) override { buffer_.status().InsertError(error); }

    const LineSequence contents() override {
      return buffer_.contents().snapshot();
    }

    LineColumn current_widget_view_start() override {
      return buffer_.editor().buffer_tree().GetActiveLeaf().view_start();
    }

    void JumpToPosition(LineColumn position) override {
      buffer_.set_position(position);
    }

   private:
    OpenBuffer& buffer_;
  };

  return MakeNonNullUnique<TerminalAdapter>(MakeNonNullUnique<Adapter>(*this),
                                            contents_);
}

double OpenBuffer::lines_read_rate() const {
  return lines_read_rate_.GetEventsPerSecond();
}

Line OpenBuffer::CurrentLine() const {
  LineNumber line = contents().AdjustLineColumn(position()).line;
  CHECK_LE(line, contents().EndLine());
  return LineAt(line).value();
}

std::optional<Line> OpenBuffer::OptionalCurrentLine() const {
  return LineAt(position().line);
}

std::optional<Line> OpenBuffer::LineAt(LineNumber line_number) const {
  if (line_number > contents_.EndLine()) return std::nullopt;
  return contents_.at(line_number);
}

LazyString OpenBuffer::ToString() const {
  return contents_.snapshot().ToLazyString();
}

const struct timespec OpenBuffer::time_last_exit() const {
  return time_last_exit_;
}

void OpenBuffer::PushSignal(UnixSignal signal) {
  status_->SetInformationText(Line{NonEmptySingleLine{signal.read()}});
  if (file_adapter_->WriteSignal(signal)) return;

  switch (signal.read()) {
    case SIGINT:
      if (child_pid_ != std::nullopt) {
        status_->SetInformationText(LineBuilder{
            SINGLE_LINE_CONSTANT(L"SIGINT >> pid:") +
            NonEmptySingleLine{
                child_pid_->read()}}.Build());
        file_system_driver()->Kill(child_pid_.value(), signal);
        return;
      }
  }

  status_->InsertError(Error{LazyString{L"Unhandled signal received: "} +
                             LazyString{std::to_wstring(signal.read())}});
}

const language::gc::Ptr<ExecutionContext>& OpenBuffer::execution_context()
    const {
  return execution_context_;
}

const NonNull<std::shared_ptr<FileSystemDriver>>&
OpenBuffer::file_system_driver() const {
  return execution_context_->file_system_driver();
}

const language::text::MutableLineSequence& OpenBuffer::contents() const {
  return contents_;
}

BufferName OpenBuffer::name() const { return options_.name; }

futures::Value<EmptyValue> OpenBuffer::SetInputFiles(
    std::optional<FileDescriptor> input_fd,
    std::optional<FileDescriptor> input_error_fd, bool fd_is_terminal,
    std::optional<ProcessId> child_pid) {
  CHECK(child_pid_ == std::nullopt);
  child_pid_ = child_pid;

  file_adapter_ = std::invoke([fd_is_terminal,
                               this] -> NonNull<std::unique_ptr<FileAdapter>> {
    if (fd_is_terminal) return NewTerminal();
    return MakeNonNullUnique<RegularFileAdapter>(RegularFileAdapter::Options{
        .thread_pool = editor().thread_pool(),
        .insert_lines = [this](auto lines_to_insert) {
          // These changes don't count: they come from disk.
          auto disk_state_freezer = FreezeDiskState();

          auto follower = GetEndPositionFollower();
          AppendToLastLine(lines_to_insert.front());
          // TODO: Avoid the linear complexity operation in the next line.
          // However, according to `tracker_erase_call`, it doesn't
          // matter much.
          auto tracker_erase_call =
              INLINE_TRACKER(FileDescriptorReader_InsertLines_Erase);
          lines_to_insert.erase(lines_to_insert.begin());  // Ugh, linear.
          tracker_erase_call = nullptr;

          AppendLines(std::move(lines_to_insert),
                      MutableLineSequence::ObserverBehavior::kHide);
        }});
  });

  auto new_reader = [this](std::optional<FileDescriptor> fd,
                           LazyString name_suffix, LineModifierSet modifiers,
                           std::unique_ptr<FileDescriptorReader>& reader) {
    if (fd == std::nullopt) {
      reader = nullptr;
      return futures::Past(EmptyValue());
    }
    futures::Future<EmptyValue> output;
    reader =
        std::make_unique<FileDescriptorReader>(FileDescriptorReader::Options{
            .name = FileDescriptorName{ToSingleLine(name()).read() +
                                       LazyString{L":"} + name_suffix},
            .fd = fd.value(),
            .receive_end_of_file =
                [root_this = NewRoot(), &reader,
                 output_consumer = std::move(output.consumer)] mutable {
                  root_this->RegisterProgress();
                  // Why make a copy? Because setting `reader` to nullptr erases
                  // us.
                  auto output_consumer_copy = std::move(output_consumer);
                  reader = nullptr;
                  std::move(output_consumer_copy)(EmptyValue());
                },
            .receive_data =
                [root_this = NewRoot(), modifiers](
                    LazyString input, std::function<void()> done_callback) {
                  root_this->RegisterProgress();
                  if (root_this->Read(buffer_variables::vm_exec)) {
                    LOG(INFO) << root_this->name()
                              << ": Evaluating VM code: " << input;
                    root_this->execution_context()->EvaluateString(input);
                  }
                  root_this->file_adapter_
                      ->ReceiveInput(std::move(input), modifiers)
                      .Transform([done_callback =
                                      std::move(done_callback)](EmptyValue) {
                        done_callback();
                        return futures::Past(EmptyValue());
                      });
                }});
    return std::move(output.value);
  };

  futures::Value<EmptyValue> end_of_file_future =
      JoinValues(new_reader(input_fd, LazyString{L"stdout"}, {}, fd_),
                 new_reader(input_error_fd, LazyString{L"stderr"},
                            {LineModifier::kBold}, fd_error_))
          .Transform([root_this =
                          NewRoot()](std::tuple<EmptyValue, EmptyValue>) {
            CHECK(root_this->fd_ == nullptr);
            CHECK(root_this->fd_error_ == nullptr);
            return VisitOptional(
                [&](ProcessId materialized_child_pid)
                    -> futures::Value<EmptyValue> {
                  return root_this->file_system_driver()
                      ->WaitPid(materialized_child_pid, 0)
                      .Transform([root_this](FileSystemDriver::WaitPidOutput
                                                 waitpid_output) {
                        root_this->child_exit_status_ = waitpid_output.wstatus;
                        clock_gettime(0, &root_this->time_last_exit_);

                        root_this->child_pid_ = std::nullopt;
                        if (root_this->on_exit_handler_.has_value()) {
                          std::invoke(
                              std::move(root_this->on_exit_handler_).value());
                          root_this->on_exit_handler_ = std::nullopt;
                        }
                        return futures::Past(Success());
                      })
                      .ConsumeErrors(
                          [](Error) { return futures::Past(EmptyValue()); });
                },
                [] { return futures::Past(EmptyValue()); },
                root_this->child_pid_);
          });
  file_adapter_->UpdateSize();  // Must follow creation of file descriptors.
  return end_of_file_future;
}

futures::Value<PossibleError> OpenBuffer::SetInputFromPath(
    const infrastructure::Path& path) {
  return OnError(file_system_driver()->Open(path, O_RDONLY | O_NONBLOCK, 0),
                 [path](Error error) {
                   LOG(INFO)
                       << path << ": SetInputFromPath: Open failed: " << error;
                   return futures::Past(error);
                 })
      .Transform([buffer = NewRoot(),
                  path](FileDescriptor fd) -> futures::Value<PossibleError> {
        LOG(INFO) << path << ": Opened file descriptor: " << fd;
        return buffer->SetInputFiles(fd, std::nullopt, false,
                                     std::optional<ProcessId>());
      });
}

const FileDescriptorReader* OpenBuffer::fd() const { return fd_.get(); }

const FileDescriptorReader* OpenBuffer::fd_error() const {
  return fd_error_.get();
}

void OpenBuffer::AddExecutionHandlers(
    infrastructure::execution::IterationHandler& handler) {
  if (fd_ != nullptr) fd_->Register(handler);
  if (fd_error_ != nullptr) fd_error_->Register(handler);
}

std::optional<infrastructure::ProcessId> OpenBuffer::child_pid() const {
  return child_pid_;
}

LineNumber OpenBuffer::current_position_line() const { return position().line; }

void OpenBuffer::set_current_position_line(LineNumber line) {
  set_current_cursor(
      LineColumn(std::min(line, LineNumber(0) + contents_.size())));
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
std::vector<URL> GetURLsForCurrentPosition(const OpenBuffer& buffer) {
  auto adjusted_position =
      buffer.contents().AdjustLineColumn(buffer.position());
  std::optional<URL> initial_url;

  NonNull<std::shared_ptr<const ParseTree>> tree = buffer.parse_tree();
  ParseTree::Route route = FindRouteToPosition(tree.value(), adjusted_position);
  for (const ParseTree* subtree : MapRoute(tree.value(), route))
    if (subtree->properties().contains(ParseTreeProperty::Link()))
      if (ValueOrError<URL> target =
              FindLinkTarget(*subtree, buffer.contents().snapshot());
          std::holds_alternative<URL>(target)) {
        initial_url = std::get<URL>(target);
        break;
      }

  if (!initial_url.has_value()) {
    LazyString line =
        GetCurrentToken({.contents = buffer.contents().snapshot(),
                         .line_column = adjusted_position,
                         .token_characters = MaterializeUnorderedSet(
                             buffer.Read(buffer_variables::path_characters))});

    if (FindLastNotOf(line, {L'/', L'.', L':'}) == std::nullopt) {
      // If there are only slashes, colons or dots, it's probably not very
      // useful to show the contents of this path.
      return {};
    }

    if (auto path = Path::New(line); !IsError(path))
      initial_url = URL::FromPath(ValueOrDie(std::move(path)));
    else
      return {};
  }

  std::vector<URL> urls_with_extensions = GetLocalFileURLsWithExtensions(
      LineSequence::BreakLines(
          buffer.Read(buffer_variables::file_context_extensions))
          .FoldLines(),
      *initial_url);

  std::vector<Path> search_paths = {};
  std::visit(overload{IgnoreErrors{},
                      [&](Path path) {
                        // Works if the current buffer is a directory listing:
                        search_paths.push_back(path);
                        // And a fall-back for the current buffer being a file:
                        std::visit(overload{IgnoreErrors{},
                                            [&](Path dir) {
                                              search_paths.push_back(dir);
                                            }},
                                   path.Dirname());
                      }},
             Path::New(buffer.Read(buffer_variables::path)));

  std::vector<URL> urls = urls_with_extensions;

  // Do the full expansion. This has square complexity, though luckily the
  // number of local_paths tends to be very small.
  for (const Path& search_path : search_paths) {
    for (const URL& url : urls_with_extensions) {
      std::visit(overload{IgnoreErrors{},
                          [&](Path path) {
                            if (path.GetRootType() != Path::RootType::kAbsolute)
                              urls.push_back(
                                  URL::FromPath(Path::Join(search_path, path)));
                          }},
                 url.GetLocalFilePath());
    }
  }
  return urls;
}

language::PossibleError CheckLocalFile(struct stat st) {
  switch (st.st_mode & S_IFMT) {
    case S_IFBLK:
      return Error{
          LazyString{L"Path for URL has unexpected type: block device\n"}};
    case S_IFCHR:
      return Error{
          LazyString{L"Path for URL has unexpected type: character device\n"}};
    case S_IFIFO:
      return Error{
          LazyString{L"Path for URL has unexpected type: FIFO/pipe\n"}};
    case S_IFSOCK:
      return Error{LazyString{L"Path for URL has unexpected type: socket\n"}};
    case S_IFLNK:
    case S_IFREG:
    case S_IFDIR:
      return Success();
    default:
      return Error{LazyString{L"Path for URL has unexpected type: unknown\n"}};
  }
}

}  // namespace

futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>>
OpenBuffer::OpenBufferForCurrentPosition(
    RemoteURLBehavior remote_url_behavior) {
  // When the cursor moves quickly, there's a race between multiple executions
  // of this logic. To avoid this, each call captures the original position
  // and uses that to avoid taking any effects when the position changes in
  // the meantime.
  auto adjusted_position = contents().AdjustLineColumn(position());
  struct Data {
    const gc::WeakPtr<OpenBuffer> source;
    ValueOrError<std::optional<gc::Root<OpenBuffer>>> output =
        std::optional<gc::Root<OpenBuffer>>();
  };
  NonNull<std::shared_ptr<Data>> data =
      MakeNonNullShared<Data>(Data{.source = ptr_this_->ToWeakPtr()});

  using ICC = futures::IterationControlCommand;
  return futures::ForEach(
             std::make_shared<std::vector<URL>>(
                 GetURLsForCurrentPosition(*this)),
             [adjusted_position, data, remote_url_behavior](const URL& url) {
               return VisitPointer(
                   data->source.Lock(),
                   [&](gc::Root<OpenBuffer> buffer) {
                     auto& editor = buffer->editor();
                     VLOG(5) << "Checking URL: " << url;
                     if (url.schema().value_or(URL::Schema::kFile) !=
                         URL::Schema::kFile) {
                       switch (remote_url_behavior) {
                         case RemoteURLBehavior::kIgnore:
                           break;
                         case RemoteURLBehavior::kLaunchBrowser:
                           editor.work_queue()->DeleteLater(
                               AddSeconds(Now(), 1.0),
                               editor.status().SetExpiringInformationText(
                                   LineBuilder{SINGLE_LINE_CONSTANT(L"Open: ") +
                                               url.read()}
                                       .Build()));
                           ForkCommand(
                               editor,
                               ForkCommandOptions{
                                   .command =
                                       LazyString{L"xdg-open "} +
                                       vm::EscapedString(ToLazyString(url))
                                           .ShellEscapedRepresentation(),
                                   .insertion_type =
                                       BuffersList::AddBufferType::kIgnore,
                               });
                       }
                       return futures::Past(ICC::kStop);
                     }
                     ValueOrError<Path> path = url.GetLocalFilePath();
                     if (std::holds_alternative<Error>(path))
                       return futures::Past(ICC::kContinue);
                     VLOG(4) << "Calling open file: " << std::get<Path>(path);
                     return OpenFileIfFound(
                                OpenFileOptions{
                                    .editor_state = editor,
                                    .path = std::get<Path>(path),
                                    .insertion_type =
                                        BuffersList::AddBufferType::kIgnore,
                                    .use_search_paths = false,
                                    .stat_validator = CheckLocalFile})
                         .Transform(
                             [data](gc::Root<OpenBuffer> buffer_context) {
                               data->output = buffer_context;
                               return futures::Past(Success(ICC::kStop));
                             })
                         .ConsumeErrors([adjusted_position, data](Error) {
                           return VisitPointer(
                               data->source.Lock(),
                               [&](gc::Root<OpenBuffer> locked_buffer) {
                                 if (adjusted_position !=
                                     locked_buffer->contents().AdjustLineColumn(
                                         locked_buffer->position())) {
                                   data->output = Error{LazyString{
                                       L"Computation was cancelled."}};
                                   return futures::Past(ICC::kStop);
                                 }
                                 return futures::Past(ICC::kContinue);
                               },
                               [] { return futures::Past(ICC::kStop); });
                         });
                   },
                   [] { return futures::Past(ICC::kStop); });
             })
      .Transform([data](ICC iteration_control_command) {
        return iteration_control_command == ICC::kContinue
                   ? Success(std::optional<gc::Root<OpenBuffer>>())
                   : std::move(data->output);
      });
}

LineColumn OpenBuffer::end_position() const {
  CHECK_GT(contents_.size(), LineNumberDelta(0));
  return LineColumn(contents_.EndLine(), contents_.back().EndColumn());
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
         child_pid_.has_value() ||
         (child_exit_status_.has_value() &&
          (!WIFEXITED(child_exit_status_.value()) ||
           WEXITSTATUS(child_exit_status_.value()) != 0));
}

std::map<BufferFlagKey, BufferFlagValue> OpenBuffer::Flags() const {
  std::map<BufferFlagKey, BufferFlagValue> output;
  if (options_.describe_status) output = options_.describe_status(*this);

  if (size_t size = undo_state_.UndoStackSize(); size > 0)
    output.insert({BufferFlagKey{SingleLine::Char<L'↶'>()},
                   BufferFlagValue{NonEmptySingleLine{size}}});

  if (size_t size = undo_state_.RedoStackSize(); size > 0)
    output.insert({BufferFlagKey{SingleLine::Char<L'↷'>()},
                   BufferFlagValue{NonEmptySingleLine{size}}});

  if (disk_state() == DiskState::kStale) {
    output.insert(
        {BufferFlagKey{SingleLine::Char<L'🐾'>()}, BufferFlagValue{}});
  }

  if (ShouldDisplayProgress()) {
    output.insert(
        {BufferFlagKey{ProgressString(Read(buffer_variables::progress),
                                      OverflowBehavior::kModulo)
                           .read()},
         BufferFlagValue{}});
  }

  if (fd() != nullptr) {
    output.insert({BufferFlagKey{SingleLine::Char<L'<'>()}, BufferFlagValue{}});
    switch (contents_.size().read()) {
      case 1:
        output.insert(
            {BufferFlagKey{SingleLine::Char<L'⚊'>()}, BufferFlagValue{}});
        break;
      case 2:
        output.insert(
            {BufferFlagKey{SINGLE_LINE_CONSTANT(L"⚌ ")}, BufferFlagValue{}});
        break;
      case 3:
        output.insert(
            {BufferFlagKey{SINGLE_LINE_CONSTANT(L"☰ ")}, BufferFlagValue{}});
        break;
      default:
        output.insert(
            {BufferFlagKey{SINGLE_LINE_CONSTANT(L"☰ ")},
             BufferFlagValue{NonEmptySingleLine{contents_.size().read()}}});
    }
    if (Read(buffer_variables::follow_end_of_file)) {
      output.insert(
          {BufferFlagKey{SingleLine::Char<L'↓'>()}, BufferFlagValue{}});
    }
    if (SingleLine pts_path =
            LineSequence::BreakLines(Read(buffer_variables::pts_path))
                .FoldLines();
        !pts_path.empty())
      output.insert({BufferFlagKey{SingleLine::Char<L'💻'>()},
                     BufferFlagValue{pts_path}});
  }

  if (work_queue()->RecentUtilization() > 0.1) {
    output.insert(
        {BufferFlagKey{SingleLine::Char<L'⏳'>()}, BufferFlagValue{}});
  }

  if (Read(buffer_variables::pin)) {
    output.insert(
        {BufferFlagKey{SingleLine::Char<L'📌'>()}, BufferFlagValue{}});
  }

  if (child_pid_.has_value()) {
    output.insert({BufferFlagKey{SingleLine::Char<L'🟡'>()},
                   BufferFlagValue{NonEmptySingleLine{child_pid_->read()}}});
  } else if (!child_exit_status_.has_value()) {
    // Nothing.
  } else if (WIFEXITED(child_exit_status_.value())) {
    auto exit_status = WEXITSTATUS(child_exit_status_.value());
    if (exit_status == 0)
      output.insert(
          {BufferFlagKey{SingleLine::Char<L'🟢'>()}, BufferFlagValue{}});
    else
      output.insert({BufferFlagKey{SingleLine::Char<L'🔴'>()},
                     BufferFlagValue{NonEmptySingleLine(exit_status)}});
  } else if (WIFSIGNALED(child_exit_status_.value())) {
    output.insert({BufferFlagKey{SingleLine::Char<L'🟣'>()},
                   BufferFlagValue{NonEmptySingleLine{
                       WTERMSIG(child_exit_status_.value())}}});
  } else {
    output.insert(
        {BufferFlagKey{SINGLE_LINE_CONSTANT(L"exit-status")},
         BufferFlagValue{NonEmptySingleLine{child_exit_status_.value()}}});
  }

  if (SingleLine marks = GetLineMarksText(); !marks.empty()) {
    output.insert(
        {BufferFlagKey{marks}, BufferFlagValue{}});  // TODO: Show better?
  }

  return output;
}

/* static */ SingleLine OpenBuffer::FlagsToString(
    std::map<BufferFlagKey, BufferFlagValue> flags) {
  return Concatenate(
      flags |
      std::views::transform(
          [](const std::pair<BufferFlagKey, BufferFlagValue>& f) {
            return f.first.read() + f.second.read();
          }) |
      Intersperse(SingleLine::Padding(ColumnNumberDelta{2})));
}

const bool& OpenBuffer::Read(const EdgeVariable<bool>* variable) const {
  return variables_.bool_variables.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<bool>* variable, bool value) {
  variables_.bool_variables.Set(variable, value);
}

void OpenBuffer::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  Set(variable, editor().modifiers().repetitions.has_value()
                    ? editor().modifiers().repetitions != 0
                    : !Read(variable));
}

const LazyString& OpenBuffer::Read(
    const EdgeVariable<LazyString>* variable) const {
  return variables_.string_variables.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<LazyString>* variable,
                     LazyString value) {
  variables_.string_variables.Set(variable, value);
}

const int& OpenBuffer::Read(const EdgeVariable<int>* variable) const {
  return variables_.int_variables.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<int>* variable, int value) {
  variables_.int_variables.Set(variable, value);
}

const double& OpenBuffer::Read(const EdgeVariable<double>* variable) const {
  return variables_.double_variables.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<double>* variable, double value) {
  variables_.double_variables.Set(variable, value);
}

const LineColumn& OpenBuffer::Read(
    const EdgeVariable<LineColumn>* variable) const {
  return variables_.line_column_variables.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<LineColumn>* variable,
                     LineColumn value) {
  variables_.line_column_variables.Set(variable, value);
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
  buffer->OpenBufferForCurrentPosition(OpenBuffer::RemoteURLBehavior::kIgnore)
      .Transform([weak_buffer = buffer.ptr().ToWeakPtr()](
                     std::optional<gc::Root<OpenBuffer>> result) {
        VisitPointer(
            weak_buffer.Lock(),
            [&result](gc::Root<OpenBuffer> locked_buffer) {
              locked_buffer->status().set_context(result);
            },
            [] {});
        return Success();
      });
}

futures::Value<EmptyValue> OpenBuffer::ApplyToCursors(
    transformation::Variant transformation,
    Modifiers::CursorsAffected cursors_affected,
    transformation::Input::Mode mode) {
  TRACK_OPERATION(OpenBuffer_ApplyToCursors);
  auto trace = log_->NewChild(LazyString{L"ApplyToCursors transformation."});
  trace->Append(LazyString{L"Transformation: "} +
                LazyString{transformation::ToString(transformation)});

  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->push_back(transformation);
  }

  undo_state_.Current()->push_front(transformation::Cursors{
      .cursors = active_cursors(), .active = position()});

  std::optional<futures::Value<EmptyValue>> transformation_result;
  if (cursors_affected == Modifiers::CursorsAffected::kAll) {
    CursorsSet single_cursor;
    CursorsSet& cursors = active_cursors();
    transformation_result = cursors_tracker_.ApplyTransformationToCursors(
        cursors, [root_this = ptr_this_->ToRoot(),
                  transformation = std::move(transformation),
                  mode](LineColumn position) {
          return root_this->Apply(transformation, position, mode)
              .Transform([root_this](transformation::Result result) {
                root_this->UpdateLastAction();
                return result.position;
              });
        });
  } else {
    VLOG(6) << "Adjusting default cursor (!multiple_cursors).";
    transformation_result =
        Apply(std::move(transformation), position(), mode)
            .Transform([root_this = ptr_this_->ToRoot()](
                           const transformation::Result& result) {
              root_this->active_cursors().MoveCurrentCursor(result.position);
              root_this->UpdateLastAction();
              return EmptyValue();
            });
  }
  return std::move(transformation_result)
      .value()
      .Transform([root_this = NewRoot()](EmptyValue) {
        if (root_this->last_transformation_stack_.empty())
          root_this->undo_state_.CommitCurrent();

        root_this->OnCursorMove();

        // This proceeds in the background but we can only start it once the
        // transformation is evaluated (since we don't know the cursor
        // position otherwise).
        StartAdjustingStatusContext(root_this);
        return EmptyValue();
      });
}

futures::Value<typename transformation::Result> OpenBuffer::Apply(
    transformation::Variant transformation, LineColumn position,
    transformation::Input::Mode mode) {
  TRACK_OPERATION(OpenBuffer_Apply);
  const std::weak_ptr<transformation::Stack> undo_stack_weak =
      undo_state_.Current().get_shared();

  // TODO(2025-05-26, easy): Change the buffer in transformation::Input to be a
  // root?
  transformation::Input input(transformation_adapter_.value(), *this);
  input.mode = mode;
  input.position = position;
  if (Read(buffer_variables::delete_into_paste_buffer)) {
    input.delete_buffer =
        editor().buffer_registry().MaybeAdd(FuturePasteBuffer{}, [&] {
          LOG(INFO) << "Creating paste buffer: " << FuturePasteBuffer{};
          return OpenBuffer::New(
              {.editor = editor(), .name = FuturePasteBuffer{}});
        });
  }

  VLOG(5) << "Apply transformation: "
          << transformation::ToString(transformation);

  return transformation::Apply(transformation, std::move(input))
      .Transform([root_this = NewRoot(),
                  transformation = std::move(transformation), mode,
                  undo_stack_weak](transformation::Result result) {
        VLOG(6) << "Got results of transformation: "
                << transformation::ToString(transformation);
        if (mode == transformation::Input::Mode::kFinal &&
            root_this->Read(buffer_variables::delete_into_paste_buffer)) {
          if (!result.added_to_paste_buffer) {
            root_this->editor().buffer_registry().Remove(FuturePasteBuffer{});
          } else if (std::optional<gc::Root<OpenBuffer>> paste_buffer =
                         root_this->editor().buffer_registry().Find(
                             FuturePasteBuffer{});
                     paste_buffer.has_value()) {
            root_this->editor().buffer_registry().Remove(FuturePasteBuffer{});
            paste_buffer->ptr()->Set(
                buffer_variables::name,
                ToSingleLine(BufferName{PasteBuffer{}}).read().read());
            root_this->editor().buffer_registry().Add(
                PasteBuffer{}, paste_buffer->ptr().ToWeakPtr());
          }
        }

        if (result.modified_buffer &&
            mode == transformation::Input::Mode::kFinal) {
          root_this->editor().StartHandlingInterrupts();
          root_this->last_transformation_ = std::move(transformation);
        }

        if (auto undo_stack = undo_stack_weak.lock(); undo_stack != nullptr) {
          undo_stack->push_front(
              transformation::Stack{.stack = result.undo_stack->stack});
          *undo_stack = transformation::Stack{
              .stack = {OptimizeBase(std::move(*undo_stack))}};
          if (result.modified_buffer)
            root_this->undo_state_.SetCurrentModifiedBuffer();
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
  last_transformation_stack_.push_back({});
}

void OpenBuffer::PopTransformationStack() {
  if (last_transformation_stack_.empty()) {
    // This can happen if the transformation stack was reset during the
    // evaluation of a transformation. For example, during an insertion, if
    // the buffer is reloaded ... that will discard the transformation stack.
    return;
  }
  last_transformation_ = std::move(last_transformation_stack_.back().value());
  last_transformation_stack_.pop_back();
  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->push_back(last_transformation_);
  } else {
    undo_state_.CommitCurrent();
  }
}

futures::Value<EmptyValue> OpenBuffer::Undo(
    UndoState::ApplyOptions::Mode undo_mode,
    UndoState::ApplyOptions::RedoMode redo_mode) {
  return undo_state_
      .Apply(UndoState::ApplyOptions{
          .mode = undo_mode,
          .redo_mode = redo_mode,
          .direction = editor().direction(),
          .repetitions = editor().repetitions().value_or(1),
          .callback =
              [root_this = NewRoot()](transformation::Variant t) {
                transformation::Input input(
                    root_this->transformation_adapter_.value(),
                    root_this.ptr().value());
                input.position = root_this->position();
                // We've undone the entire changes, so...
                root_this->last_transformation_stack_.clear();
                root_this->undo_state_.AbandonCurrent();
                return transformation::Apply(t, input);
              }})
      .Transform([root_this = NewRoot()](EmptyValue) {
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
  return language::gc::Root<const OpenBuffer>(ptr_this_->ToRoot());
}

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
OpenBuffer::Expand() const {
  return {environment().object_metadata(), default_commands_.object_metadata(),
          mode_.object_metadata(), execution_context_.object_metadata()};
}

const std::multimap<LineColumn, LineMarks::Mark>& OpenBuffer::GetLineMarks()
    const {
  return editor().line_marks().GetMarksForTargetBuffer(name());
}

const std::multimap<LineColumn, LineMarks::ExpiredMark>&
OpenBuffer::GetExpiredLineMarks() const {
  return editor().line_marks().GetExpiredMarksForTargetBuffer(name());
}

SingleLine OpenBuffer::GetLineMarksText() const {
  size_t marks = GetLineMarks().size();
  size_t expired_marks = GetExpiredLineMarks().size();
  SingleLine output;
  if (marks > 0 || expired_marks > 0) {
    output = SINGLE_LINE_CONSTANT(L"marks:") + NonEmptySingleLine{marks};
    if (expired_marks > 0)
      output += Parenthesize(NonEmptySingleLine{expired_marks}).read();
  }
  return output;
}

const language::gc::Ptr<vm::Environment>& OpenBuffer::environment() const {
  return execution_context_->environment();
}

bool OpenBuffer::IsPastPosition(LineColumn position) const {
  return position != LineColumn::Max() &&
         (position.line < contents_.EndLine() ||
          (position.line == contents_.EndLine() &&
           position.column <= LineAt(position.line)->EndColumn()));
}

void OpenBuffer::UpdateLastAction() {
  auto now = Now();
  if (now < last_action_) return;
  last_action_ = now;
  if (double idle_seconds = Read(buffer_variables::close_after_idle_seconds);
      idle_seconds >= 0.0) {
    work_queue()->Schedule(WorkQueue::Callback{
        .time = AddSeconds(Now(), idle_seconds),
        .callback = gc::LockCallback(
            gc::BindFront(
                editor().gc_pool(),
                [last_action = last_action_](gc::Root<OpenBuffer> buffer_root) {
                  OpenBuffer& buffer = buffer_root.ptr().value();
                  if (buffer.last_action_ != last_action) return;
                  buffer.last_action_ = Now();
                  LOG(INFO) << "close_after_idle_seconds: Closing.";
                  buffer.editor().CloseBuffer(buffer);
                },
                ptr_this_->ToWeakPtr())
                .ptr())});
  }
}
void OpenBuffer::OnCursorMove() {
  BufferWidget& leaf = editor().buffer_tree().GetActiveLeaf();
  static const VisualOverlayPriority kPriority = VisualOverlayPriority(0);
  static const VisualOverlayKey kKey = VisualOverlayKey(L"token");
  visual_overlay_map_[kPriority].erase(kKey);
  if (std::optional<language::gc::Root<OpenBuffer>> leaf_buffer = leaf.Lock();
      leaf_buffer.has_value() && &leaf_buffer->ptr().value() == this) {
    LineColumn view_start = leaf.view_start();
    VisitPointer(
        display_data().view_size().Get(),
        [&](LineColumnDelta view_size) {
          std::set<language::text::Range> ranges =
              buffer_syntax_parser_.GetRangesForToken(
                  contents().AdjustLineColumn(position()),
                  Range(view_start, view_start + view_size));
          for (const language::text::Range& range : ranges) {
            CHECK(range.IsSingleLine());
            visual_overlay_map_[kPriority][kKey].insert(
                {range.begin(),
                 VisualOverlay{.content = ColumnNumberDelta(
                                   range.end().column - range.begin().column),
                               .modifiers = {LineModifier::kUnderline},
                               .behavior = VisualOverlay::Behavior::kOn}});
          }
        },
        [] {});
  }
}

NonNull<std::unique_ptr<EditorState>> EditorForTests(
    std::optional<infrastructure::Path> config_path) {
  static audio::Player* player = audio::NewNullPlayer().get_unique().release();
  return EditorState::New(std::invoke([config_path] {
                            CommandLineValues output;
                            if (config_path.has_value())
                              output.config_paths = {config_path.value()};
                            return output;
                          }),
                          *player);
}

gc::Root<OpenBuffer> NewBufferForTests(EditorState& editor) {
  gc::Root<OpenBuffer> output = OpenBuffer::New(
      {.editor = editor,
       .name = editor.buffer_registry().NewAnonymousBufferName()});
  editor.AddBuffer(output, BuffersList::AddBufferType::kVisit);
  return output;
}

}  // namespace afc::editor
