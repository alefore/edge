#include "src/editor.h"

#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <ranges>
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

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor_vm.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/audio.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/time.h"
#include "src/language/container.h"
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/server.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"
#include "src/widget_list.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;
namespace error = afc::language::error;

using afc::concurrent::ThreadPool;
using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::concurrent::WorkQueue;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::UnixSignal;
using afc::language::EmptyValue;
using afc::language::EraseOrDie;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language ::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Observers;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::Pointer;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::view::SkipErrors;
using error::FromOptional;

namespace afc::editor {
using ::operator<<;

namespace {
std::weak_ordering BufferComparePinFirst(const OpenBuffer& a,
                                         const OpenBuffer& b) {
  if (a.Read(buffer_variables::pin) && !b.Read(buffer_variables::pin))
    return std::weak_ordering::less;
  if (b.Read(buffer_variables::pin) && !a.Read(buffer_variables::pin))
    return std::weak_ordering::greater;
  return std::weak_ordering::equivalent;
}

#if 0
std::weak_ordering BufferCompareLastVisit(const OpenBuffer& a,
                                          const OpenBuffer& b) {
  if (auto pin_order = BufferComparePinFirst(a, b);
      pin_order != std::weak_ordering::equivalent)
    return pin_order;
  return a.last_visit() <=> b.last_visit();
}
#endif

std::weak_ordering BufferCompareLastVisitInverted(const OpenBuffer& a,
                                                  const OpenBuffer& b) {
  if (auto pin_order = BufferComparePinFirst(a, b);
      pin_order != std::weak_ordering::equivalent)
    return pin_order;
  return b.last_visit() <=> a.last_visit();
}

std::weak_ordering BufferCompareAlphabetic(const OpenBuffer& a,
                                           const OpenBuffer& b) {
  if (auto pin_order = BufferComparePinFirst(a, b);
      pin_order != std::weak_ordering::equivalent)
    return pin_order;
  return a.name() <=> b.name();
}

BufferRegistry::BufferComparePredicate GetBufferComparePredicate(
    LazyString name) {
  return name == LazyString{L"last_visit"} ? BufferCompareLastVisitInverted
                                           : BufferCompareAlphabetic;
}
}  // namespace

/* static */
PathComponent EditorState::StatePathComponent() {
  return PathComponent::FromString(L"state");
}

std::optional<language::gc::Root<InputReceiver>>
EditorState::keyboard_redirect() const {
  return keyboard_redirect_;
}

std::optional<language::gc::Root<InputReceiver>>
EditorState::set_keyboard_redirect(
    std::optional<language::gc::Root<InputReceiver>> keyboard_redirect) {
  return std::exchange(keyboard_redirect_, std::move(keyboard_redirect));
}

// Executes pending work from all buffers.
void EditorState::ExecutePendingWork() {
  VLOG(5) << "Executing pending work.";
  TRACK_OPERATION(EditorState_ExecutePendingWork);
  work_queue()->Execute();
}

std::optional<struct timespec> EditorState::WorkQueueNextExecution() const {
  using Output = std::optional<struct timespec>;
  return container::Fold(
      [](struct timespec a, Output output) -> Output {
        return output.has_value() ? std::min(a, output.value()) : a;
      },
      Output(),
      buffer_registry().buffers() | gc::view::Value |
          std::views::transform(
              [](OpenBuffer& buffer) -> ValueOrError<struct timespec> {
                return FromOptional(buffer.work_queue()->NextExecution());
              }) |
          SkipErrors);
}

const NonNull<std::shared_ptr<WorkQueue>>& EditorState::work_queue() const {
  return thread_pool().work_queue();
}

ThreadPoolWithWorkQueue& EditorState::thread_pool() const {
  return thread_pool_.value();
}

/* static */
void EditorState::NotifyInternalEvent(EditorState::SharedData& data) {
  VLOG(5) << "Internal event notification!";
  if (!data.has_internal_events.lock([](bool& value) {
        bool old_value = value;
        value = true;
        return old_value;
      }) &&
      data.pipe_to_communicate_internal_events.has_value() &&
      write(data.pipe_to_communicate_internal_events->second.read(), " ", 1) ==
          -1) {
    data.status->InsertError(
        Error{LazyString{L"Write to internal pipe failed: "} +
              LazyString{FromByteString(strerror(errno))}});
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

class BuffersListAdapter : public BuffersList::CustomerAdapter {
 public:
  BuffersListAdapter(EditorState& editor) : editor_(editor) {}

  std::vector<gc::Root<OpenBuffer>> active_buffers() override {
    return editor_.active_buffers();
  }

  bool multiple_buffers_mode() override {
    return editor_.Read(editor_variables::multiple_buffers);
  }

 private:
  const EditorState& editor_;
};

/* static */ NonNull<std::unique_ptr<EditorState>> EditorState::New(
    CommandLineValues args, infrastructure::audio::Player& audio_player) {
  auto thread_pool = MakeNonNullShared<ThreadPoolWithWorkQueue>(
      MakeNonNullShared<ThreadPool>(32), WorkQueue::New());
  NonNull<std::unique_ptr<language::gc::Pool>> gc_pool =
      MakeNonNullUnique<language::gc::Pool>(gc::Pool::Options{
          .collect_duration_threshold = 0.05,
          .operation_factory =
              std::move(MakeNonNullUnique<concurrent::OperationFactory>(
                            thread_pool->thread_pool())
                            .get_unique())});
  gc::Root<vm::Environment> environment = BuildEditorEnvironment(
      gc_pool.value(),
      MakeNonNullUnique<FileSystemDriver>(thread_pool.value()));

  return MakeNonNullUnique<EditorState>(ConstructorAccessTag{}, args,
                                        audio_player, thread_pool,
                                        std::move(gc_pool), environment.ptr());
}

EditorState::EditorState(
    EditorState::ConstructorAccessTag, CommandLineValues args,
    infrastructure::audio::Player& audio_player,
    NonNull<std::shared_ptr<ThreadPoolWithWorkQueue>> thread_pool,
    NonNull<std::unique_ptr<language::gc::Pool>> gc_pool,
    gc::Ptr<vm::Environment> environment)
    : args_(std::move(args)),
      gc_pool_(std::move(gc_pool)),
      shared_data_(MakeNonNullShared<SharedData>(SharedData{
          .pipe_to_communicate_internal_events = std::invoke(
              [] -> std::optional<std::pair<FileDescriptor, FileDescriptor>> {
                int output[2];
                if (pipe2(output, O_NONBLOCK) == -1) return std::nullopt;
                return std::make_pair(FileDescriptor(output[0]),
                                      FileDescriptor(output[1]));
              }),
          .status = MakeNonNullShared<Status>(audio_player)})),
      string_variables_(editor_variables::StringStruct()->NewInstance()),
      bool_variables_(editor_variables::BoolStruct()->NewInstance()),
      int_variables_(editor_variables::IntStruct()->NewInstance()),
      double_variables_(editor_variables::DoubleStruct()->NewInstance()),
      edge_path_(args_.config_paths),
      thread_pool_(std::move(thread_pool)),
      execution_context_(ExecutionContext::New(
          std::invoke([&] {
            environment->Define(
                vm::Identifier{
                    NonEmptySingleLine{SingleLine{LazyString{L"editor"}}}},
                vm::Value::NewObject(
                    gc_pool_.value(),
                    vm::VMTypeMapper<editor::EditorState>::object_type_name,
                    NonNull<std::shared_ptr<EditorState>>::Unsafe(
                        std::shared_ptr<EditorState>(this, [](void*) {}))));
            return environment;
          }),
          shared_data_->status.get_shared(), thread_pool_->work_queue(),
          MakeNonNullUnique<FileSystemDriver>(thread_pool_.value()))),
      default_commands_(NewCommandMode(*this)),
      audio_player_(audio_player),
      buffer_registry_(gc_pool_->NewRoot(MakeNonNullUnique<BufferRegistry>(
          GetBufferComparePredicate(Read(editor_variables::buffer_sort_order)),
          [](const OpenBuffer& b) { return b.dirty(); }))),
      buffer_tree_(buffer_registry_.ptr().value(),
                   MakeNonNullUnique<BuffersListAdapter>(*this)) {
  work_queue()->OnSchedule().Add([shared_data = shared_data_] {
    NotifyInternalEvent(shared_data.value());
    return Observers::State::kAlive;
  });
  auto paths = edge_path();
  futures::ForEach(paths.begin(), paths.end(), [this](Path dir) {
    auto path =
        Path::Join(dir, ValueOrDie(Path::New(LazyString{L"hooks/start.cc"})));
    return execution_context_->EvaluateFile(path)
        .Transform([](gc::Root<vm::Value>) {
          return futures::Past(
              Success(futures::IterationControlCommand::kContinue));
        })
        .ConsumeErrors([](Error) {
          return futures::Past(futures::IterationControlCommand::kContinue);
        });
  });

  double_variables_.ObserveValue(editor_variables::volume).Add([this] {
    audio_player_.SetVolume(infrastructure::audio::Volume(
        std::max(0.0, std::min(1.0, Read(editor_variables::volume)))));
    return Observers::State::kAlive;
  });

  ReclaimAndSchedule(execution_context().pool(), work_queue().value());
}

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  LOG(INFO) << "Closing buffers.";
  std::ranges::for_each(buffer_registry().buffers() | gc::view::Value,
                        &OpenBuffer::Close);

  // TODO(2025-05-23, trivial): Add operator-> to gc::Root and get rid of the
  // ptr() bits.
  execution_context()
      ->environment()
      ->Clear();  // We may have loops. This helps break them.
  buffer_registry_->Clear();

  LOG(INFO) << "Reclaim GC pool.";
  execution_context().pool().FullCollect();
  execution_context().pool().BlockUntilDone();

  LOG(INFO) << "Destructor finished running.";
}

const CommandLineValues& EditorState::args() { return args_; }

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

const LazyString& EditorState::Read(
    const EdgeVariable<LazyString>* variable) const {
  return string_variables_.Get(variable);
}

void EditorState::Set(const EdgeVariable<LazyString>* variable,
                      LazyString value) {
  string_variables_.Set(variable, std::move(value));
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
      [](gc::Root<OpenBuffer> buffer) { buffer->CheckPosition(); }, [] {});
}

void EditorState::CloseBuffer(OpenBuffer& buffer) {
  LOG(INFO) << "Close buffer: " << buffer.name();
  OnError(buffer.PrepareToClose(),
          [buffer = buffer.NewRoot()](Error error)
              -> futures::ValueOrError<OpenBuffer::PrepareToCloseOutput> {
            error = AugmentError(
                LazyString{L"🖝  Unable to close (“*ad” to ignore): "} +
                    buffer->Read(buffer_variables::name),
                error);
            switch (buffer->status().InsertError(error, 30)) {
              case error::Log::InsertResult::kInserted:
                return futures::Past(error);
              case error::Log::InsertResult::kAlreadyFound:
                return futures::Past(OpenBuffer::PrepareToCloseOutput());
            }
            LOG(FATAL) << "Invalid enum value.";
            return futures::Past(error);
          })
      .Transform(
          [this, buffer = buffer.NewRoot()](OpenBuffer::PrepareToCloseOutput) {
            buffer->Close();
            buffer_registry_->RemoveListedBuffers(std::unordered_set{
                NonNull<const OpenBuffer*>::AddressOf(buffer.ptr().value())});
            AdjustWidgets();
            return futures::Past(Success());
          });
}

gc::Root<OpenBuffer> EditorState::FindOrBuildBuffer(
    BufferName name, OnceOnlyFunction<gc::Root<OpenBuffer>()> callback) {
  LOG(INFO) << "FindOrBuildBuffer: " << name;
  return buffer_registry_->MaybeAdd(name, std::move(callback));
}

void EditorState::set_current_buffer(gc::Root<OpenBuffer> buffer,
                                     CommandArgumentModeApplyMode apply_mode) {
  if (apply_mode == CommandArgumentModeApplyMode::kFinal) {
    buffer->Visit();
  }
  buffer_tree_.GetActiveLeaf().SetBuffer(buffer.ptr().ToWeakPtr());
  AdjustWidgets();
}

void EditorState::SetActiveBuffer(size_t position) {
  set_current_buffer(buffer_registry_->GetListedBuffer(
                         position % buffer_registry_->ListedBuffersCount()),
                     CommandArgumentModeApplyMode::kFinal);
}

void EditorState::AdvanceActiveBuffer(int delta) {
  // TODO(2025-05-20): This function isn't really thread-safe.
  if (buffer_registry_->ListedBuffersCount() <= 1) return;
  delta += buffer_tree().GetCurrentIndex();
  size_t total = buffer_registry_->ListedBuffersCount();
  if (delta < 0) {
    delta = total - ((-delta) % total);
  } else {
    delta %= total;
  }
  set_current_buffer(buffer_registry_->GetListedBuffer(delta % total),
                     CommandArgumentModeApplyMode::kFinal);
}

void EditorState::AdjustWidgets() {
  buffer_registry_->SetListedSortOrder(
      GetBufferComparePredicate(Read(editor_variables::buffer_sort_order)));

  auto buffers_to_retain = Read(editor_variables::buffers_to_retain);
  buffer_registry_->SetListedCount(buffers_to_retain >= 0
                                       ? size_t(buffers_to_retain)
                                       : std::optional<size_t>());
  auto buffers_to_show = Read(editor_variables::buffers_to_show);
  buffer_registry_->SetShownCount(
      buffers_to_show >= 1 ? size_t(buffers_to_show) : std::optional<size_t>());
  buffer_tree_.Update();
}

bool EditorState::has_current_buffer() const {
  return current_buffer().has_value();
}
std::optional<gc::Root<OpenBuffer>> EditorState::current_buffer() const {
  return buffer_tree_.active_buffer();
}

std::vector<gc::Root<OpenBuffer>> EditorState::active_buffers() const {
  if (status().GetType() == Status::Type::kPrompt) {
    return {status().prompt_buffer().value()};
  } else if (Read(editor_variables::multiple_buffers)) {
    return buffer_registry_->LockListedBuffers(
        [](std::vector<gc::Root<OpenBuffer>> buffers) { return buffers; });
  } else if (std::optional<gc::Root<OpenBuffer>> buffer = current_buffer();
             buffer.has_value()) {
    if (buffer->ptr()->status().GetType() == Status::Type::kPrompt) {
      buffer = buffer->ptr()->status().prompt_buffer()->ptr().ToRoot();
    }
    return {buffer.value()};
  }
  return {};
}

void EditorState::AddBuffer(gc::Root<OpenBuffer> buffer,
                            BuffersList::AddBufferType insertion_type) {
  std::vector<gc::Root<OpenBuffer>> initial_active_buffers = active_buffers();
  if (insertion_type != BuffersList::AddBufferType::kIgnore)
    buffer_registry().AddListedBuffer(buffer);
  buffer_tree().AddBuffer(buffer, insertion_type);
  AdjustWidgets();

  // If the set of buffers changed and some keyboard redirect was active, ...
  // cancel it. Perhaps the keyboard redirect should have a method to react to
  // this, so that it can decide what to do? Then again, does it make sense for
  // any of them to do anything other than be canceled?
  if (initial_active_buffers != active_buffers())
    set_keyboard_redirect(std::nullopt);
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
    gc::Root<OpenBuffer> buffer = buffer_registry_->GetListedBuffer(
        (std::max(modifiers().repetitions.value(), 1ul) - 1) %
        buffer_registry_->ListedBuffersCount());
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

void EditorState::set_exit_value(int exit_value) { exit_value_ = exit_value; }

std::optional<LazyString> EditorState::GetExitNotice() const {
  if (dirty_buffers_saved_to_backup_.empty()) return std::nullopt;
  return LazyString{L"Dirty contents backed up (in "} +
         Path::Join(edge_path()[0], StatePathComponent()).read() +
         LazyString{L"):\n"} +
         Concatenate(dirty_buffers_saved_to_backup_ |
                     std::views::transform([](const BufferName& name) {
                       return LazyString{L"  "} + ToSingleLine(name).read() +
                              LazyString{L"\n"};
                     }));
}

void EditorState::Terminate(TerminationType termination_type, int exit_value) {
  status().SetInformationText(
      LineBuilder(
          SingleLine{LazyString{L"Exit: Preparing to close buffers ("}} +
          SingleLine{
              LazyString{std::to_wstring(buffer_registry().buffers().size())}} +
          SingleLine{LazyString{L")"}})
          .Build());
  if (termination_type == TerminationType::kWhenClean) {
    LOG(INFO) << "Checking buffers for termination.";
    if (auto buffers_with_problems =
            buffer_registry().buffers() | gc::view::Value |
            std::views::transform([](OpenBuffer& buffer) {
              return NonNull<OpenBuffer*>::AddressOf(buffer);
            }) |
            std::views::filter([](const NonNull<OpenBuffer*>& buffer) {
              return IsError(buffer->status().LogErrors(
                  AugmentError(LazyString{L"Unable to close"},
                               buffer->IsUnableToPrepareToClose())));
            });
        !buffers_with_problems.empty()) {
      switch (status().InsertError(
          Error{LazyString{L"🖝  Dirty buffers (pre):"} +
                Concatenate(container::MaterializeVector(
                    buffers_with_problems |
                    std::views::transform(
                        [](const NonNull<OpenBuffer*>& buffer) -> LazyString {
                          return LazyString{L" "} +
                                 buffer->Read(buffer_variables::name);
                        })))},
          30)) {
        case error::Log::InsertResult::kInserted:
          return;
        case error::Log::InsertResult::kAlreadyFound:
          break;
      }
    }
  }

  struct Data {
    TerminationType termination_type;
    int exit_value;
    std::vector<Error> errors = {};
    std::vector<gc::Root<OpenBuffer>> buffers_with_problems = {};
    std::set<gc::Root<OpenBuffer>> pending_buffers = {};
  };

  auto data =
      MakeNonNullShared<Data>(Data{.termination_type = termination_type,
                                   .exit_value = exit_value,
                                   .pending_buffers = container::MaterializeSet(
                                       buffer_registry().buffers())});
  CHECK_EQ(buffer_registry().buffers().size(), data->pending_buffers.size());
  for (const gc::Root<OpenBuffer>& buffer : buffer_registry().buffers()) {
    LOG(INFO) << "Preparing to close: " << buffer->name() << " @ "
              << &buffer.ptr().value();
    CHECK_EQ(data->pending_buffers.count(buffer), 1ul);
    buffer->PrepareToClose()
        .Transform(
            [this, data, buffer](OpenBuffer::PrepareToCloseOutput output) {
              if (output.dirty_contents_saved_to_backup)
                dirty_buffers_saved_to_backup_.insert(buffer->name());
              return futures::Past(Success());
            })
        .ConsumeErrors([data, buffer](Error error) {
          data->errors.push_back(error);
          data->buffers_with_problems.push_back(buffer);
          return futures::Past(EmptyValue());
        })
        .Transform([this, data, buffer](EmptyValue) {
          LOG(INFO) << "Removing buffer: " << buffer->name()
                    << &buffer.ptr().value();
          EraseOrDie(data->pending_buffers, buffer);

          if (data->pending_buffers.empty()) {
            if (data->termination_type == TerminationType::kIgnoringErrors) {
              exit_value_ = data->exit_value;
              return futures::Past(EmptyValue());
            }
            LOG(INFO) << "Checking buffers state for termination.";
            if (!data->buffers_with_problems.empty()) {
              switch (status().InsertError(
                  Error{LazyString{L"🖝  Dirty buffers (post):"} +
                        Concatenate(std::move(data->buffers_with_problems) |
                                    gc::view::Value |
                                    std::views::transform(
                                        [](const OpenBuffer& b) -> LazyString {
                                          return LazyString{L" "} +
                                                 ToSingleLine(b.name()).read();
                                        }))},
                  5)) {
                case error::Log::InsertResult::kInserted:
                  return futures::Past(EmptyValue());
                case error::Log::InsertResult::kAlreadyFound:
                  break;
              }
            }
            LOG(INFO) << "Terminating.";
            status().SetInformationText(Line{SingleLine{
                LazyString{L"Exit: All buffers closed, shutting down."}}});
            exit_value_ = data->exit_value;
            return futures::Past(EmptyValue());
          }

          const size_t max_buffers_to_show = 5;
          status().SetInformationText(
              LineBuilder(
                  SINGLE_LINE_CONSTANT(L"Exit: Closing buffers: Remaining: ") +
                  NonEmptySingleLine{data->pending_buffers.size()} +
                  SINGLE_LINE_CONSTANT(L": ") +
                  Concatenate(
                      data->pending_buffers |
                      std::views::take(max_buffers_to_show) |
                      std::views::transform(
                          [](const gc::Root<OpenBuffer>& pending_buffer) {
                            return ToSingleLine(pending_buffer->name()).read();
                          }) |
                      Intersperse(SINGLE_LINE_CONSTANT(L", "))) +
                  (data->pending_buffers.size() > max_buffers_to_show
                       ? SINGLE_LINE_CONSTANT(L"…")
                       : SingleLine{}))
                  .Build());
          return futures::Past(EmptyValue());
        });
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

futures::Value<EmptyValue> EditorState::ProcessInput(
    const std::vector<ExtendedChar>& input) {
  return ProcessInput(std::make_shared<std::vector<ExtendedChar>>(input), 0);
}

futures::Value<EmptyValue> EditorState::ProcessInput(
    std::shared_ptr<std::vector<infrastructure::ExtendedChar>> input,
    size_t start_index) {
  while (start_index < input->size()) {
    InputReceiver* receiver = keyboard_redirect_.has_value()
                                  ? &keyboard_redirect_->ptr().value()
                                  : nullptr;
    if (std::optional<language::gc::Root<OpenBuffer>> buffer = current_buffer();
        receiver == nullptr && buffer.has_value())
      receiver = &buffer->ptr()->mode();

    if (receiver != nullptr) {
      size_t advance = receiver->Receive(*input, start_index);
      CHECK_GT(advance, 0ul);
      start_index += advance;
      CHECK_LE(start_index, input->size());
      continue;
    }

    return OpenAnonymousBuffer(*this).Transform(
        [this, input, start_index](gc::Root<OpenBuffer> buffer) {
          if (!has_current_buffer()) {
            buffer_registry_->AddListedBuffer(buffer);
            set_current_buffer(buffer, CommandArgumentModeApplyMode::kFinal);
            CHECK(has_current_buffer());
            CHECK(&current_buffer().value().ptr().value() ==
                  &buffer.ptr().value());
          }
          return ProcessInput(input, start_index);
        });
  }
  return futures::Past(EmptyValue());
}

std::optional<EditorState::ScreenState> EditorState::FlushScreenState() {
  auto now = Now();
  if (now < next_screen_update_) {
    // This is enough to cause the main loop to wake up; it'll attempt to do a
    // redraw then. Multiple attempts may be scheduled, but that's fine (just
    // a bit wasteful of memory).
    work_queue()->Wait(next_screen_update_);
    return {};
  }
  next_screen_update_ = AddSeconds(now, 1.0 / args_.frames_per_second);
  ScreenState output = screen_state_;
  screen_state_ = ScreenState();
  return output;
}

gc::Root<OpenBuffer> EditorState::GetConsole() {
  return FindOrBuildBuffer(ConsoleBufferName{}, [&] {
    gc::Root<OpenBuffer> buffer_root =
        OpenBuffer::New({.editor = *this, .name = ConsoleBufferName{}});
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.Set(buffer_variables::allow_dirty_delete, true);
    buffer.Set(buffer_variables::show_in_buffers_list, false);
    buffer.Set(buffer_variables::persist_state, false);
    return buffer_root;
  });
}

Status& EditorState::status() { return shared_data_->status.value(); }
const Status& EditorState::status() const {
  return shared_data_->status.value();
}

const infrastructure::Path& EditorState::home_directory() const {
  return args_.home_directory;
}

language::gc::Pool& EditorState::gc_pool() { return execution_context_.pool(); }

language::gc::Root<vm::Environment> EditorState::environment() {
  // TODO(2025-05-23, trivial): Consider switching this method to return a Ptr.
  return execution_context_->environment().ToRoot();
}

language::gc::Root<ExecutionContext> EditorState::execution_context() {
  return execution_context_;
}

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
  return std::ranges::any_of(active_buffers(), [](auto& buffer) {
    return buffer->Read(buffer_variables::pts);
  });
}

void EditorState::ExecutionIteration(
    infrastructure::execution::IterationHandler& handler) {
  // We execute pending work before updating screens, since we expect that the
  // pending work updates may have visible effects.
  ExecutePendingWork();

  std::ranges::for_each(
      buffer_registry().buffers() | gc::view::Value,
      [&handler](OpenBuffer& buffer) { buffer.AddExecutionHandlers(handler); });

  if (shared_data_->pipe_to_communicate_internal_events.has_value())
    handler.AddHandler(
        shared_data_->pipe_to_communicate_internal_events->first,
        POLLIN | POLLPRI, [&](int) {
          char buffer[4096];
          VLOG(5) << "Internal events detected.";
          while (read(shared_data_->pipe_to_communicate_internal_events->first
                          .read(),
                      buffer, sizeof(buffer)) > 0)
            continue;
          shared_data_->has_internal_events.lock(
              [](bool& value) { value = false; });
        });
}

BufferRegistry& EditorState::buffer_registry() {
  return buffer_registry_.ptr().value();
}

const BufferRegistry& EditorState::buffer_registry() const {
  return buffer_registry_.ptr().value();
}
}  // namespace afc::editor
