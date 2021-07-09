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
#include "src/char_buffer.h"
#include "src/command_with_modifiers.h"
#include "src/cpp_parse_tree.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/futures/futures.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/lazy_string_functional.h"
#include "src/line_marks.h"
#include "src/map_mode.h"
#include "src/parsers/diff.h"
#include "src/parsers/markdown.h"
#include "src/run_command_handler.h"
#include "src/screen.h"
#include "src/screen_vm.h"
#include "src/seek.h"
#include "src/server.h"
#include "src/status.h"
#include "src/substring.h"
#include "src/time.h"
#include "src/tokenize.h"
#include "src/tracker.h"
#include "src/transformation.h"
#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/stack.h"
#include "src/viewers.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"
#include "src/vm_transformation.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
struct BufferWrapper {
  std::shared_ptr<editor::OpenBuffer> buffer;
};

std::shared_ptr<editor::OpenBuffer>
VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(Value* value) {
  return static_cast<BufferWrapper*>(value->user_value.get())->buffer;
}

/* static */ Value::Ptr VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
    std::shared_ptr<editor::OpenBuffer> value) {
  auto wrapper = std::make_shared<BufferWrapper>();
  wrapper->buffer = std::move(value);
  return Value::NewObject(L"Buffer",
                          std::shared_ptr<void>(wrapper, wrapper.get()));
}

const VMType VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype =
    VMType::ObjectType(L"Buffer");
}  // namespace vm
namespace editor {
using futures::IterationControlCommand;

namespace {
static const wchar_t* kOldCursors = L"old-cursors";

using std::unordered_set;

template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    EdgeStruct* edge_struct, afc::vm::ObjectType* object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  VMType buffer_type = VMType::ObjectType(object_type);

  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type->AddField(
        variable->name(),
        vm::NewCallback([reader, variable](std::shared_ptr<OpenBuffer> buffer) {
          DVLOG(4) << "Buffer field reader is returning.";
          return (buffer.get()->*reader)(variable);
        }));

    // Setter.
    object_type->AddField(
        L"set_" + variable->name(),
        vm::NewCallback([variable, setter](std::shared_ptr<OpenBuffer> buffer,
                                           FieldValue value) {
          (buffer.get()->*setter)(variable, value);
        }));
  }
}
}  // namespace

using namespace afc::vm;
using std::to_wstring;

/* static */ const wstring OpenBuffer::kBuffersName = L"- buffers";

// Name of the buffer that holds the contents that the paste command should
// paste, which corresponds to things that have been deleted recently.
/* static */ const wstring OpenBuffer::kPasteBuffer = L"- paste buffer";

// Name of the buffer that holds the contents that have been deleted recently
// and which should still be included in the delete buffer for additional
// deletions.
//
// This is used so that multiple subsequent deletion transformations (without
// any interspersed non-delete transformations) will all aggregate into the
// paste buffer (rather than retaining only the deletion corresponding to the
// last such transformation).
/* static */ const wstring kFuturePasteBuffer = L"- future paste buffer";

/* static */ void OpenBuffer::RegisterBufferType(
    EditorState* editor_state, afc::vm::Environment* environment) {
  auto buffer = std::make_unique<ObjectType>(L"Buffer");

  RegisterBufferFields<EdgeStruct<bool>, bool>(buffer_variables::BoolStruct(),
                                               buffer.get(), &OpenBuffer::Read,
                                               &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<wstring>, wstring>(
      buffer_variables::StringStruct(), buffer.get(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<int>, int>(buffer_variables::IntStruct(),
                                             buffer.get(), &OpenBuffer::Read,
                                             &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<double>, double>(
      buffer_variables::DoubleStruct(), buffer.get(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<LineColumn>, LineColumn>(
      buffer_variables::LineColumnStruct(), buffer.get(), &OpenBuffer::Read,
      &OpenBuffer::Set);

  buffer->AddField(
      L"SetStatus",
      vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer, wstring s) {
        buffer->status()->SetInformationText(s);
      }));

  buffer->AddField(
      L"SetWarningStatus",
      vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer, std::wstring s) {
        buffer->status()->SetWarningText(s);
      }));

  buffer->AddField(
      L"line_count", vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
        return static_cast<int>(buffer->contents()->size().line_delta);
      }));

  buffer->AddField(L"set_position",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer,
                                      LineColumn position) {
                     buffer->set_position(position);
                   }));

  buffer->AddField(L"position",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     return LineColumn(buffer->position());
                   }));

  buffer->AddField(
      L"line",
      vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer, int line_input) {
        LineNumber line =
            min(LineNumber(max(line_input, 0)),
                LineNumber(0) + buffer->lines_size() - LineNumberDelta(1));
        return buffer->contents()->at(line)->ToString();
      }));

  buffer->AddField(
      L"ApplyTransformation",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get()),
           vm::VMTypeMapper<editor::transformation::Variant*>::vmtype},
          [](std::vector<std::unique_ptr<Value>> args, Trampoline*) {
            CHECK_EQ(args.size(), 2ul);
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            auto transformation = static_cast<editor::transformation::Variant*>(
                args[1]->user_value.get());
            return futures::Transform(
                buffer->ApplyToCursors(*transformation),
                futures::Past(EvaluationOutput::Return(Value::NewVoid())));
          }));

#if 0
  buffer->AddField(
      L"GetRegion",
      Value::NewFunction(
          {VMType::ObjectType(L"Range"), VMType::ObjectType(buffer.get()),
           VMType::String()},
          [](vector<Value::Ptr> args, Trampoline* trampoline) {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMType::ObjectType(L"Buffer"));
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            // TODO: Don't ignore the buffer! Apply it to it!
            // auto buffer =
            // static_cast<OpenBuffer*>(args[0]->user_value.get());
            auto resume = trampoline->Interrupt();
            NewCommandWithModifiers(
                args[1]->str, L"Selects a region",
                [resume](EditorState*, OpenBuffer* buffer,
                         CommandApplyMode mode, Modifiers modifiers) {
                  // TODO: Apply this to all cursors. That's tricky, because we
                  // don't know what effect each transformation will have, and
                  // because we can't call `resume` more than once (it will
                  // likely free things when we call it).
                  if (mode == CommandApplyMode::FINAL) {
                    LOG(INFO) << "GetRegion: Resuming.";
                    resume(Value::NewObject(
                        L"Range", std::make_shared<Range>(
                             buffer->FindPartialRange(
                                 modifiers, buffer->position()))));
                  } else {
                    buffer->PushTransformationStack();
                    DeleteOptions options;
                    options.modifiers = modifiers;
                    options.copy_to_paste_buffer = false;
                    buffer->ApplyToCursors(
                        NewDeleteTransformation(options),
                        Modifiers::AFFECT_ONLY_CURRENT_CURSOR,
                        transformation::Input::Mode::kPreview,
                        [buffer] () {
                      buffer->PopTransformationStack();
                    });
                  }
                })
                ->ProcessInput(L'\n', editor_state);
          }));
#endif

  buffer->AddField(L"PushTransformationStack",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     buffer->PushTransformationStack();
                   }));

  buffer->AddField(L"PopTransformationStack",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     buffer->PopTransformationStack();
                   }));

  buffer->AddField(
      L"AddKeyboardTextTransformer",
      Value::NewFunction(
          {VMType::Bool(), VMType::ObjectType(buffer.get()),
           VMType::Function({VMType::String(), VMType::String()})},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            CHECK_EQ(args[0]->type, VMType::ObjectType(L"Buffer"));
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            CHECK(buffer != nullptr);
            return Value::NewBool(
                buffer->AddKeyboardTextTransformer(std::move(args[1])));
          }));

  buffer->AddField(
      L"Filter",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get()),
           VMType::Function({VMType::Bool(), VMType::String()})},
          [](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            CHECK_EQ(args[0]->type, VMType::ObjectType(L"Buffer"));
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            CHECK(buffer != nullptr);
            buffer->set_filter(std::move(args[1]));
            return Value::NewVoid();
          }));

  buffer->AddField(L"Reload",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     if (buffer->editor()->structure() == StructureLine()) {
                       auto target_buffer = buffer->GetBufferFromCurrentLine();
                       if (target_buffer != nullptr) {
                         buffer = target_buffer;
                       }
                     }
                     buffer->Reload();
                     buffer->editor()->ResetModifiers();
                   }));

  buffer->AddField(L"SendEndOfFileToProcess",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     if (buffer->editor()->structure() == StructureLine()) {
                       auto target_buffer = buffer->GetBufferFromCurrentLine();
                       if (target_buffer != nullptr) {
                         buffer = target_buffer;
                       }
                     }
                     buffer->SendEndOfFileToProcess();
                     buffer->editor()->ResetModifiers();
                   }));

  buffer->AddField(
      L"Save",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get())},
          [](vector<Value::Ptr> args, Trampoline*) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            if (buffer->editor()->structure() == StructureLine()) {
              auto target_buffer = buffer->GetBufferFromCurrentLine();
              if (target_buffer != nullptr) {
                buffer = target_buffer;
              }
            }

            futures::Future<EvaluationOutput> future;
            buffer->Save().SetConsumer(
                [consumer = std::move(future.consumer)](PossibleError result) {
                  consumer(result.IsError()
                               ? EvaluationOutput::Abort(result.error())
                               : EvaluationOutput::Return(Value::NewVoid()));
                });
            buffer->editor()->ResetModifiers();
            return future.value;
          }));

  buffer->AddField(L"Close",
                   vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
                     if (buffer->editor()->structure() == StructureLine()) {
                       auto target_buffer = buffer->GetBufferFromCurrentLine();
                       if (target_buffer != nullptr) {
                         buffer = target_buffer;
                       }
                     }
                     buffer->editor()->CloseBuffer(buffer.get());
                     buffer->editor()->ResetModifiers();
                   }));

  buffer->AddField(
      L"AddBinding",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get()), VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [editor_state](vector<unique_ptr<Value>> args) {
            CHECK_EQ(args.size(), 4u);
            CHECK_EQ(args[0]->type, VMType::ObjectType(L"Buffer"));
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            CHECK_EQ(args[2]->type, VMType::VM_STRING);
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            CHECK(buffer != nullptr);
            buffer->default_commands_->Add(args[1]->str, args[2]->str,
                                           std::move(args[3]),
                                           buffer->environment_);
            return Value::NewVoid();
          }));

  buffer->AddField(
      L"AddBindingToFile",
      vm::NewCallback([editor_state](std::shared_ptr<OpenBuffer> buffer,
                                     wstring keys, wstring path) {
        LOG(INFO) << "AddBindingToFile: " << keys << " -> " << path;
        buffer->default_commands_->Add(
            keys,
            [buffer, path](EditorState* editor_state) {
              wstring resolved_path;
              auto options = ResolvePathOptions::New(
                  editor_state, std::make_shared<FileSystemDriver>(
                                    editor_state->work_queue()));
              options.path = path;
              futures::OnError(
                  futures::Transform(ResolvePath(std::move(options)),
                                     [buffer, path](ResolvePathOutput results) {
                                       buffer->EvaluateFile(results.path);
                                       return Success();
                                     }),
                  [buffer, path](Error error) {
                    buffer->status()->SetWarningText(L"Unable to resolve: " +
                                                     path + L": " +
                                                     error.description);
                    return Success();
                  });
            },
            L"Load file: " + path);
      }));

  buffer->AddField(
      L"ShowTrackers", vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer) {
        for (auto& data : Tracker::GetData()) {
          buffer->AppendLine(StringAppend(
              StringAppend(NewLazyString(data.name), NewLazyString(L": ")),
              NewLazyString(std::to_wstring(data.executions)),
              NewLazyString(L" "),
              NewLazyString(std::to_wstring(data.seconds))));
        }
      }));

  buffer->AddField(
      L"EvaluateFile",
      vm::NewCallback([](std::shared_ptr<OpenBuffer> buffer, wstring path_str) {
        auto path = Path::FromString(path_str);
        if (path.IsError()) {
          LOG(ERROR) << path.error().description;
          return;
        }
        buffer->EvaluateFile(std::move(path.value()));
      }));

  buffer->AddField(
      L"WaitForEndOfFile",
      Value::NewFunction(
          {VMType::Void(), VMType::ObjectType(buffer.get())},
          [](vector<Value::Ptr> args, Trampoline*) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer =
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::get(
                    args[0].get());
            futures::Future<EvaluationOutput> future;
            buffer->AddEndOfFileObserver(
                [consumer = std::move(future.consumer)] {
                  consumer(EvaluationOutput::Return(Value::NewVoid()));
                });
            return future.value;
          }));

  environment->DefineType(L"Buffer", std::move(buffer));
}

/* static */ std::shared_ptr<OpenBuffer> OpenBuffer::New(Options options) {
  auto output =
      std::make_shared<OpenBuffer>(ConstructorAccessTag(), std::move(options));
  output->Initialize();
  return output;
}

namespace {
void Execute(std::shared_ptr<OpenBuffer> buffer) {
  buffer->work_queue()->Execute();
  if (auto next = buffer->work_queue()->NextExecution(); next.has_value()) {
    auto editor_queue = buffer->editor()->work_queue();
    editor_queue->ScheduleAt(
        next.value(),
        [buffer = std::move(buffer)]() mutable { Execute(std::move(buffer)); });
  }
}
}  // namespace

OpenBuffer::OpenBuffer(ConstructorAccessTag, Options options)
    : options_(std::move(options)),
      work_queue_([this] {
        auto next_execution = work_queue_.NextExecution();
        if (!next_execution.has_value()) {
          // Must have lost a race against the other thread. This happens when
          // the thread that is scheduling work is not the main thread.
          return;
        }
        options_.editor->work_queue()->ScheduleAt(
            next_execution.value(),
            [shared_this = shared_from_this()]() mutable {
              Execute(std::move(shared_this));
            });
      }),
      contents_([this](const CursorsTracker::Transformation& transformation) {
        if (syntax_data_state_ == SyntaxDataState::kDone) {
          syntax_data_state_ = SyntaxDataState::kPending;
          work_queue_.Schedule([this] {
            syntax_data_state_ = SyntaxDataState::kDone;
            CHECK(tree_parser_ != nullptr);
            MaybeStartUpdatingSyntaxTrees();
          });
        }
        SetDiskState(DiskState::kStale);
        if (Read(buffer_variables::persist_state)) {
          switch (backup_state_) {
            case DiskState::kCurrent: {
              backup_state_ = DiskState::kStale;
              auto flush_backup_time = Now();
              flush_backup_time.tv_sec += 30;
              work_queue_.ScheduleAt(flush_backup_time,
                                     [shared_this = shared_from_this()] {
                                       shared_this->UpdateBackup();
                                     });
            } break;

            case DiskState::kStale:
              break;  // Nothing.
          }
        }
        last_action_ = Now();
        cursors_tracker_.AdjustCursors(transformation);
      }),
      bool_variables_(buffer_variables::BoolStruct()->NewInstance()),
      string_variables_(buffer_variables::StringStruct()->NewInstance()),
      int_variables_(buffer_variables::IntStruct()->NewInstance()),
      double_variables_(buffer_variables::DoubleStruct()->NewInstance()),
      line_column_variables_(
          buffer_variables::LineColumnStruct()->NewInstance()),
      environment_(
          std::make_shared<Environment>(options_.editor->environment())),
      filter_version_(0),
      last_transformation_(NewNoopTransformation()),
      default_commands_(options_.editor->default_commands()->NewChild()),
      mode_(std::make_unique<MapMode>(default_commands_)),
      status_(options_.editor->GetConsole(), options_.editor->audio_player()),
      syntax_data_(L"SyntaxData", &work_queue_),
      async_read_evaluator_(L"ReadEvaluator", &work_queue_),
      file_system_driver_(&work_queue_) {}

OpenBuffer::~OpenBuffer() {
  LOG(INFO) << "Start destructor.";
  environment_->Clear();
}

EditorState* OpenBuffer::editor() const { return options_.editor; }

Status* OpenBuffer::status() const { return &status_; }

PossibleError OpenBuffer::IsUnableToPrepareToClose() const {
  if (options_.editor->modifiers().strength > Modifiers::Strength::kNormal) {
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
    return futures::Past(is_unable);
  }

  return futures::Transform(
      options_.editor->modifiers().strength == Modifiers::Strength::kNormal
          ? PersistState()
          : futures::IgnoreErrors(PersistState()),
      [this](EmptyValue) {
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
          CHECK(options_.editor->modifiers().strength >
                Modifiers::Strength::kNormal);
        }
        if (!dirty() || !Read(buffer_variables::save_on_close)) {
          return futures::Past(ValueOrError(Success()));
        }
        LOG(INFO) << Read(buffer_variables::name)
                  << ": attempting to save buffer.";
        return Save();
      });
}

void OpenBuffer::Close() {
  log_->Append(L"Closing");
  if (dirty() && Read(buffer_variables::save_on_close)) {
    log_->Append(L"Saving buffer: " + Read(buffer_variables::name));
    Save();
  }
  std::vector<std::function<void()>> close_observers;
  close_observers.swap(close_observers_);
  for (auto& observer : close_observers) {
    observer();
  }
}

void OpenBuffer::AddEndOfFileObserver(std::function<void()> observer) {
  if (fd_ == nullptr && fd_error_ == nullptr &&
      reload_state_ == ReloadState::kDone) {
    observer();
    return;
  }
  end_of_file_observers_.push_back(std::move(observer));
}

void OpenBuffer::AddCloseObserver(std::function<void()> observer) {
  close_observers_.push_back(std::move(observer));
}

void OpenBuffer::Enter() {
  if (Read(buffer_variables::reload_on_enter)) {
    Reload();
    CheckPosition();
  }
}

void OpenBuffer::Visit() {
  Enter();
  last_visit_ = last_action_ = Now();
  if (options_.handle_visit != nullptr) {
    options_.handle_visit(this);
  }
}

struct timespec OpenBuffer::last_visit() const {
  return last_visit_;
}
struct timespec OpenBuffer::last_action() const {
  return last_action_;
}

futures::Value<PossibleError> OpenBuffer::PersistState() const {
  auto trace = log_->NewChild(L"Persist State");
  if (!Read(buffer_variables::persist_state)) {
    return futures::Past(ValueOrError(Success()));
  }

  auto edge_state_directory = GetEdgeStateDirectory();
  if (edge_state_directory.IsError()) {
    status_.SetWarningText(edge_state_directory.error().description);
    return futures::Past(PossibleError(edge_state_directory.error()));
  }

  auto path = Path::Join(edge_state_directory.value(),
                         PathComponent::FromString(L".edge_state").value());
  LOG(INFO) << "PersistState: Preparing state file: " << path;
  BufferContents contents;
  contents.push_back(L"// State of file: " + path.ToString());
  contents.push_back(L"");

  contents.push_back(L"buffer.set_position(" + position().ToCppString() +
                     L");");
  contents.push_back(L"");

  contents.push_back(L"// String variables");
  for (const auto& variable : buffer_variables::StringStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(\"" +
                       CppEscapeString(Read(variable.second.get())) + L"\");");
  }
  contents.push_back(L"");

  contents.push_back(L"// Int variables");
  for (const auto& variable : buffer_variables::IntStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(" +
                       std::to_wstring(Read(variable.second.get())) + L");");
  }
  contents.push_back(L"");

  contents.push_back(L"// Bool variables");
  for (const auto& variable : buffer_variables::BoolStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(" +
                       (Read(variable.second.get()) ? L"true" : L"false") +
                       L");");
  }
  contents.push_back(L"");

  contents.push_back(L"// LineColumn variables");
  for (const auto& variable :
       buffer_variables::LineColumnStruct()->variables()) {
    contents.push_back(L"buffer.set_" + variable.first + L"(" +
                       Read(variable.second.get()).ToCppString() + L");");
  }
  contents.push_back(L"");

  return OnError(SaveContentsToFile(path, contents, work_queue()),
                 [this](Error error) {
                   status()->SetWarningText(L"Unable to persist state: " +
                                            error.description);
                   return error;
                 });
}

void OpenBuffer::ClearContents(
    BufferContents::CursorsBehavior cursors_behavior) {
  VLOG(5) << "Clear contents of buffer: " << Read(buffer_variables::name);
  options_.editor->line_marks()->RemoveExpiredMarksFromSource(
      Read(buffer_variables::name));
  options_.editor->line_marks()->ExpireMarksFromSource(
      *this, Read(buffer_variables::name));
  contents_.EraseLines(LineNumber(0), LineNumber(0) + contents_.size(),
                       cursors_behavior);
  if (terminal_ != nullptr) {
    terminal_->SetPosition(LineColumn());
  }
  last_transformation_ = NewNoopTransformation();
  last_transformation_stack_.clear();
  undo_past_.clear();
  undo_future_.clear();
}

void OpenBuffer::AppendEmptyLine() {
  auto follower = GetEndPositionFollower();
  contents_.push_back(std::make_shared<Line>());
}

void OpenBuffer::EndOfFile() {
  last_action_ = Now();
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
  editor()->line_marks()->RemoveExpiredMarksFromSource(
      Read(buffer_variables::name));

  vector<std::function<void()>> observers;
  observers.swap(end_of_file_observers_);
  for (auto& observer : observers) {
    observer();
  }

  if (Read(buffer_variables::reload_after_exit)) {
    Set(buffer_variables::reload_after_exit,
        Read(buffer_variables::default_reload_after_exit));
    Reload();
  }
  if (Read(buffer_variables::close_after_clean_exit) &&
      child_exit_status_.has_value() && WIFEXITED(child_exit_status_.value()) &&
      WEXITSTATUS(child_exit_status_.value()) == 0) {
    editor()->CloseBuffer(this);
  }

  auto current_buffer = editor()->current_buffer();
  if (current_buffer != nullptr &&
      current_buffer->Read(buffer_variables::name) == kBuffersName) {
    current_buffer->Reload();
  }
}

void OpenBuffer::SendEndOfFileToProcess() {
  if (fd() == nullptr) {
    status()->SetInformationText(L"No active subprocess for current buffer.");
    return;
  }
  if (Read(buffer_variables::pts)) {
    char str[1] = {4};
    if (write(fd()->fd(), str, sizeof(str)) == -1) {
      status()->SetInformationText(L"Sending EOF failed: " +
                                   FromByteString(strerror(errno)));
      return;
    }
    status()->SetInformationText(L"EOF sent");
  } else {
    if (shutdown(fd()->fd(), SHUT_WR) == -1) {
      status()->SetInformationText(L"shutdown(SHUT_WR) failed: " +
                                   FromByteString(strerror(errno)));
      return;
    }
    status()->SetInformationText(L"shutdown sent");
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

void OpenBuffer::ReadData() { ReadData(&fd_); }
void OpenBuffer::ReadErrorData() { ReadData(&fd_error_); }

void OpenBuffer::UpdateTreeParser() {
  auto parser = Read(buffer_variables::tree_parser);
  std::wistringstream typos_stream(Read(buffer_variables::typos));
  std::unordered_set<wstring> typos_set{
      std::istream_iterator<std::wstring, wchar_t>(typos_stream),
      std::istream_iterator<std::wstring, wchar_t>()};
  if (parser == L"text") {
    tree_parser_ = NewLineTreeParser(
        NewWordsTreeParser(Read(buffer_variables::symbol_characters), typos_set,
                           NewNullTreeParser()));
  } else if (parser == L"cpp") {
    std::wistringstream keywords(Read(buffer_variables::language_keywords));
    tree_parser_ =
        NewCppTreeParser(std::unordered_set<wstring>(
                             std::istream_iterator<wstring, wchar_t>(keywords),
                             std::istream_iterator<wstring, wchar_t>()),
                         std::move(typos_set));
  } else if (parser == L"diff") {
    tree_parser_ = parsers::NewDiffTreeParser();
  } else if (parser == L"md") {
    tree_parser_ = parsers::NewMarkdownTreeParser();
  } else {
    tree_parser_ = NewNullTreeParser();
  }
}

std::shared_ptr<const ParseTree> OpenBuffer::parse_tree() const {
  return parse_tree_;
}

std::shared_ptr<const ParseTree> OpenBuffer::simplified_parse_tree() const {
  return simplified_parse_tree_;
}

void OpenBuffer::Initialize() {
  UpdateTreeParser();

  // We use the aliasing constructor or else ... we'll never actually be
  // reclaimed.
  environment_->Define(
      L"buffer",
      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
          std::shared_ptr<OpenBuffer>(std::shared_ptr<OpenBuffer>(), this)));

  Set(buffer_variables::name, options_.name);
  if (options_.path.has_value()) {
    Set(buffer_variables::path, options_.path.value().ToString());
  }
  Set(buffer_variables::pts_path, L"");
  Set(buffer_variables::command, L"");
  Set(buffer_variables::reload_after_exit, false);
  if (Read(buffer_variables::name) == kPasteBuffer ||
      Read(buffer_variables::name) == kFuturePasteBuffer) {
    Set(buffer_variables::allow_dirty_delete, true);
    Set(buffer_variables::show_in_buffers_list, false);
    Set(buffer_variables::delete_into_paste_buffer, false);
  }
  ClearContents(BufferContents::CursorsBehavior::kUnmodified);

  if (auto buffer_path = Path::FromString(Read(buffer_variables::path));
      !buffer_path.IsError()) {
    FileSystemDriver file_system_driver(editor()->work_queue());
    for (const auto& dir : options_.editor->edge_path()) {
      auto state_path = Path::Join(
          Path::Join(dir, PathComponent::FromString(L"state").value()),
          Path::Join(buffer_path.value(),
                     PathComponent::FromString(L".edge_state").value()));
      // TODO: Ensure that the buffer won't be deleted before the calls to
      // EvaluateFile are done?
      futures::Transform(file_system_driver.Stat(state_path),
                         [state_path, this](struct stat) {
                           auto results = EvaluateFile(state_path);
                           if (results.has_value()) {
                             return futures::Transform(
                                 results.value(), futures::Past(Success()));
                           }
                           return futures::Past(Success());
                         });
    }
  }
}

void OpenBuffer::MaybeStartUpdatingSyntaxTrees() {
  if (TreeParser::IsNull(tree_parser_.get())) return;

  struct Output {
    std::shared_ptr<const ParseTree> parse_tree;
    std::shared_ptr<const ParseTree> simplified_parse_tree;
  };

  syntax_data_
      .Run(std::function<Output(void)>(
          [contents = std::shared_ptr<BufferContents>(contents_.copy()),
           parser = tree_parser_] {
            static Tracker tracker(
                L"OpenBuffer::MaybeStartUpdatingSyntaxTrees::produce");
            auto tracker_call = tracker.Call();
            VLOG(3) << "Executing parse tree update.";
            Output output;
            output.parse_tree = std::make_shared<ParseTree>(
                parser->FindChildren(*contents, contents->range()));
            output.simplified_parse_tree =
                std::make_shared<ParseTree>(SimplifyTree(*output.parse_tree));
            return output;
          }))
      .SetConsumer([this](Output output) {
        LOG(INFO) << "Installing new parse trees.";
        parse_tree_ = std::move(output.parse_tree);
        simplified_parse_tree_ = std::move(output.simplified_parse_tree);
      });
}

void OpenBuffer::StartNewLine(std::shared_ptr<Line> line) {
  static Tracker tracker(L"OpenBuffer::StartNewLine");
  auto tracker_call = tracker.Call();

  CHECK(line != nullptr);
  DVLOG(5) << "Line is completed: " << contents_.back()->ToString();

  AppendLines({line});
}

void OpenBuffer::AppendLines(std::vector<std::shared_ptr<const Line>> lines) {
  static Tracker tracker(L"OpenBuffer::AppendLines");
  auto tracker_call = tracker.Call();

  auto lines_added = LineNumberDelta(lines.size());
  if (lines_added.IsZero()) return;

  LineNumberDelta start_new_section = contents_.size() - LineNumberDelta(1);
  contents_.append_back(std::move(lines));
  if (Read(buffer_variables::contains_line_marks)) {
    static Tracker tracker(L"OpenBuffer::StartNewLine::ScanForMarks");
    auto tracker_call = tracker.Call();
    auto options = ResolvePathOptions::New(
        editor(), std::make_shared<FileSystemDriver>(editor()->work_queue()));
    auto buffer_name = Read(buffer_variables::name);
    for (LineNumberDelta i; i < lines_added; ++i) {
      auto source_line = LineNumber() + start_new_section + i;
      options.path = contents_.at(source_line)->ToString();
      futures::Transform(ResolvePath(options),
                         [editor = editor(), buffer_name,
                          source_line](ResolvePathOutput results) {
                           LineMarks::Mark mark;
                           mark.source = buffer_name;
                           mark.source_line = source_line;
                           mark.target_buffer = results.path.ToString();
                           if (results.position.has_value()) {
                             mark.target = *results.position;
                           }
                           LOG(INFO) << "Found a mark: " << mark;
                           editor->line_marks()->AddMark(mark);
                           return Success();
                         });
    }
  }
}

void OpenBuffer::Reload() {
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

  auto paths = editor()->edge_path();
  futures::Transform(
      futures::ForEach(
          paths.begin(), paths.end(),
          [this](Path dir) {
            if (auto value = EvaluateFile(Path::Join(
                    dir, Path::FromString(L"hooks/buffer-reload.cc").value()));
                value.has_value()) {
              return futures::Transform(
                  value.value(),
                  futures::Past(IterationControlCommand::kContinue));
            }
            return Past(IterationControlCommand::kContinue);
          }),
      [this](IterationControlCommand) -> futures::ValueOrError<EmptyValue> {
        if (editor()->exit_value().has_value()) return futures::Past(Success());
        SetDiskState(DiskState::kCurrent);
        LOG(INFO) << "Starting reload: " << Read(buffer_variables::name);
        return options_.generate_contents != nullptr
                   ? IgnoreErrors(options_.generate_contents(this))
                   : futures::Past(Success());
      },
      [this](EmptyValue) {
        return futures::OnError(
            futures::Transform(futures::Past(GetEdgeStateDirectory()),
                               [this](Path dir) {
                                 return options_.log_supplier(&work_queue_,
                                                              dir);
                               }),
            [](Error error) {
              LOG(INFO) << "Error opening log: " << error.description;
              return Success(NewNullLog());
            });
      },
      [this](std::unique_ptr<Log> log) -> futures::ValueOrError<EmptyValue> {
        CHECK(log != nullptr);
        log_ = std::move(log);
        return futures::Past(Success());
      },
      [shared_this = shared_from_this(), this](EmptyValue) {
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
        return futures::Past(Success());
      });
}

futures::Value<PossibleError> OpenBuffer::Save() {
  LOG(INFO) << "Saving buffer: " << Read(buffer_variables::name);
  if (options_.handle_save == nullptr) {
    status_.SetWarningText(L"Buffer can't be saved.");
    return futures::Past(PossibleError(Error(L"Buffer can't be saved.")));
  }
  return options_.handle_save(
      {.buffer = this, .save_type = Options::SaveType::kMainFile});
}

ValueOrError<Path> OpenBuffer::GetEdgeStateDirectory() const {
  auto path_vector = editor()->edge_path();
  if (path_vector.empty()) {
    return Error(L"Empty edge path.");
  }
  auto path = path_vector[0];
  ASSIGN_OR_RETURN(
      auto file_path,
      AugmentErrors(
          std::wstring{L"Unable to persist buffer with invalid path "} +
              (dirty() ? L" (dirty)" : L" (clean)") + L" " +
              (disk_state_ == DiskState::kStale ? L"modified"
                                                : L"not modified"),
          AbsolutePath::FromString(Read(buffer_variables::path))));

  if (file_path.GetRootType() != Path::RootType::kAbsolute) {
    return Error(L"Unable to persist buffer without absolute path: " +
                 file_path.ToString());
  }

  ASSIGN_OR_RETURN(
      auto file_path_components,
      AugmentErrors(L"Unable to split path", file_path.DirectorySplit()));
  file_path_components.push_front(PathComponent::FromString(L"state").value());

  LOG(INFO) << "GetEdgeStateDirectory: Preparing directory for state: " << path;
  for (auto& component : file_path_components) {
    path = Path::Join(path, component);
    struct stat stat_buffer;
    auto path_byte_string = ToByteString(path.ToString());
    // TODO(easy): Use async stat.
    if (stat(path_byte_string.c_str(), &stat_buffer) != -1) {
      if (S_ISDIR(stat_buffer.st_mode)) {
        continue;
      }
      return Error(L"Oops, exists, but is not a directory: " + path.ToString());
    }
    if (mkdir(path_byte_string.c_str(), 0700)) {
      return Error(L"mkdir failed: " + FromByteString(strerror(errno)) + L": " +
                   path.ToString());
    }
  }
  return Success(path);
}

Log* OpenBuffer::log() const { return log_.get(); }

void OpenBuffer::UpdateBackup() {
  CHECK(backup_state_ == DiskState::kStale);
  log_->Append(L"UpdateBackup starts.");
  if (options_.handle_save != nullptr) {
    options_.handle_save(
        {.buffer = this, .save_type = Options::SaveType::kBackup});
  }
  backup_state_ = DiskState::kCurrent;
}

void OpenBuffer::AppendLazyString(std::shared_ptr<LazyString> input) {
  ColumnNumber start;
  ForEachColumn(*input, [&](ColumnNumber i, wchar_t c) {
    CHECK_GE(i, start);
    if (c == '\n') {
      AppendLine(Substring(input, start, i - start));
      start = i + ColumnNumberDelta(1);
    }
  });
  AppendLine(Substring(input, start));
}

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  wstring str = str_input->ToString();
}

void OpenBuffer::SortContents(LineNumber first, LineNumber last,
                              std::function<bool(const shared_ptr<const Line>&,
                                                 const shared_ptr<const Line>&)>
                                  compare) {
  CHECK(first <= last);
  contents_.sort(first, last, compare);
}

LineNumberDelta OpenBuffer::lines_size() const { return contents_.size(); }

LineNumber OpenBuffer::EndLine() const { return contents_.EndLine(); }

void OpenBuffer::EraseLines(LineNumber first, LineNumber last) {
  CHECK_LE(first, last);
  CHECK_LE(last, LineNumber(0) + contents_.size());
  contents_.EraseLines(first, last, BufferContents::CursorsBehavior::kAdjust);
}

void OpenBuffer::InsertLine(LineNumber line_position, shared_ptr<Line> line) {
  contents_.insert_line(line_position, line);
}

void OpenBuffer::AppendLine(shared_ptr<LazyString> str) {
  CHECK(str != nullptr);
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

void OpenBuffer::AppendRawLine(std::shared_ptr<LazyString> str) {
  AppendRawLine(std::make_shared<Line>(Line::Options(str)));
}

void OpenBuffer::AppendRawLine(std::shared_ptr<Line> line) {
  auto follower = GetEndPositionFollower();
  contents_.push_back(line);
}

void OpenBuffer::AppendToLastLine(std::shared_ptr<LazyString> str) {
  AppendToLastLine(Line(Line::Options(std::move(str))));
}

void OpenBuffer::AppendToLastLine(Line line) {
  static Tracker tracker(L"OpenBuffer::AppendToLastLine");
  auto tracker_call = tracker.Call();
  auto follower = GetEndPositionFollower();
  contents_.AppendToLine(LineNumber(0) + contents_.size(), std::move(line));
}

std::unique_ptr<Expression> OpenBuffer::CompileString(
    const std::wstring& code, std::wstring* error_description) {
  return afc::vm::CompileString(code, environment_, error_description);
}

futures::Value<std::unique_ptr<Value>> OpenBuffer::EvaluateExpression(
    Expression* expr) {
  return Evaluate(expr, environment_,
                  [work_queue = work_queue()](std::function<void()> callback) {
                    work_queue->Schedule(std::move(callback));
                  });
}

std::optional<futures::Value<std::unique_ptr<Value>>>
OpenBuffer::EvaluateString(const wstring& code) {
  wstring error_description;
  LOG(INFO) << "Compiling code.";
  auto expression = CompileString(code, &error_description);
  if (expression == nullptr) {
    status_.SetWarningText(L"🐜Compilation error: " + error_description);
    return std::nullopt;
  }
  LOG(INFO) << "Code compiled, evaluating.";
  return EvaluateExpression(expression.get());
}

std::optional<futures::Value<std::unique_ptr<Value>>> OpenBuffer::EvaluateFile(
    const Path& path) {
  wstring error_description;
  std::shared_ptr<Expression> expression = CompileFile(
      ToByteString(path.ToString()), environment_, &error_description);
  if (expression == nullptr) {
    status_.SetWarningText(path.ToString() + L": error: " + error_description);
    return std::nullopt;
  }
  LOG(INFO) << Read(buffer_variables::path) << " ("
            << Read(buffer_variables::name) << "): Evaluating file: " << path;
  return Evaluate(
      expression.get(), environment_,
      [path, work_queue = work_queue()](std::function<void()> resume) {
        LOG(INFO) << "Evaluation of file yields: " << path;
        work_queue->Schedule(std::move(resume));
      });
}

WorkQueue* OpenBuffer::work_queue() const { return &work_queue_; }

OpenBuffer::LockFunction OpenBuffer::GetLockFunction() {
  return [this](std::function<void(OpenBuffer*)> callback) {
    work_queue()->Schedule(
        [this, callback = std::move(callback)]() { callback(this); });
  };
}

void OpenBuffer::DeleteRange(const Range& range) {
  if (range.begin.line == range.end.line) {
    contents_.DeleteCharactersFromLine(range.begin,
                                       range.end.column - range.begin.column);
  } else {
    contents_.DeleteToLineEnd(range.begin);
    contents_.DeleteCharactersFromLine(LineColumn(range.end.line),
                                       range.end.column.ToDelta());
    // Lines in the middle.
    EraseLines(range.begin.line + LineNumberDelta(1), range.end.line);
    contents_.FoldNextLine(range.begin.line);
  }
}

LineColumn OpenBuffer::InsertInPosition(
    const OpenBuffer& buffer, const LineColumn& input_position,
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
  contents_.insert(position.line.next(), buffer.contents_, modifiers);
  contents_.FoldNextLine(position.line);

  LineNumber last_line =
      position.line + buffer.contents_.size() - LineNumberDelta(1);
  CHECK_LE(last_line, EndLine());
  auto line = LineAt(last_line);
  CHECK(line != nullptr);
  ColumnNumber column = line->EndColumn();

  contents_.FoldNextLine(last_line);
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
  auto line = std::make_shared<Line>(*LineAt(position.line));
  if (line->EndColumn() > position.column + ColumnNumberDelta(1)) {
    return;
  }
  line->Append(Line(ColumnNumberDelta::PaddingString(
      position.column - line->EndColumn() + ColumnNumberDelta(1), L' ')));

  contents_.set_line(position.line, line);
}

void OpenBuffer::CheckPosition() {
  if (position().line > contents_.EndLine()) {
    set_position(LineColumn(contents_.EndLine()));
  }
}

CursorsSet* OpenBuffer::FindOrCreateCursors(const wstring& name) {
  return cursors_tracker_.FindOrCreateCursors(name);
}

const CursorsSet* OpenBuffer::FindCursors(const wstring& name) const {
  return cursors_tracker_.FindCursors(name);
}

CursorsSet* OpenBuffer::active_cursors() {
  return const_cast<CursorsSet*>(
      const_cast<const OpenBuffer*>(this)->active_cursors());
}

const CursorsSet* OpenBuffer::active_cursors() const {
  return FindCursors(options_.editor->modifiers().active_cursors);
}

void OpenBuffer::set_active_cursors(const vector<LineColumn>& positions) {
  if (positions.empty()) {
    return;
  }
  auto cursors = active_cursors();
  FindOrCreateCursors(kOldCursors)->swap(cursors);
  cursors->clear();
  cursors->insert(positions.begin(), positions.end());

  // We find the first position (rather than just take cursors->begin()) so that
  // we start at the first requested position.
  cursors->SetCurrentCursor(positions.front());
}

void OpenBuffer::ToggleActiveCursors() {
  LineColumn desired_position = position();

  auto cursors = active_cursors();
  FindOrCreateCursors(kOldCursors)->swap(cursors);

  // TODO: Maybe it'd be best to pick the nearest after the cursor?
  // TODO: This should probably be merged somewhat with set_active_cursors?
  for (auto it = cursors->begin(); it != cursors->end(); ++it) {
    if (desired_position == *it) {
      LOG(INFO) << "Desired position " << desired_position << " prevails.";
      cursors->SetCurrentCursor(desired_position);
      CHECK_LE(position().line, LineNumber(0) + lines_size());
      return;
    }
  }

  cursors->SetCurrentCursor(*cursors->begin());
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
  const auto& marks = *GetLineMarks();
  if (marks.empty()) {
    status_.SetWarningText(L"Buffer has no marks!");
    return;
  }

  std::vector<LineColumn> cursors;
  for (auto& it : marks) {
    cursors.push_back(it.second.target);
  }
  set_active_cursors(cursors);
}

void OpenBuffer::set_current_cursor(LineColumn new_value) {
  auto cursors = active_cursors();
  // Need to do find + erase because cursors is a multiset; we only want to
  // erase one cursor, rather than all cursors with the current value.
  cursors->erase(position());
  cursors->insert(new_value);
  cursors->SetCurrentCursor(new_value);
}

void OpenBuffer::CreateCursor() {
  if (options_.editor->modifiers().structure == StructureChar()) {
    CHECK_LE(position().line, LineNumber(0) + contents_.size());
    active_cursors()->insert(position());
  } else {
    auto structure = options_.editor->modifiers().structure;
    Modifiers tmp_modifiers = options_.editor->modifiers();
    tmp_modifiers.structure = StructureCursor();
    Range range = FindPartialRange(tmp_modifiers, position());
    if (range.IsEmpty()) {
      return;
    }
    options_.editor->set_direction(Direction::kForwards);
    LOG(INFO) << "Range for cursors: " << range;
    while (!range.IsEmpty()) {
      auto tmp_first = range.begin;
      structure->SeekToNext(this, Direction::kForwards, &tmp_first);
      if (tmp_first > range.begin && tmp_first < range.end) {
        VLOG(5) << "Creating cursor at: " << tmp_first;
        active_cursors()->insert(tmp_first);
      }
      if (!structure->SeekToLimit(this, Direction::kForwards, &tmp_first)) {
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
  auto cursors = active_cursors();
  CHECK(!cursors->empty());

  size_t index = 0;
  auto output = cursors->begin();
  while (output != cursors->end() &&
         (*output < position ||
          (direction == Direction::kForwards && *output == position &&
           std::next(output) != cursors->end() &&
           *std::next(output) == position))) {
    ++output;
    ++index;
  }

  size_t repetitions = modifiers.repetitions.value_or(1) % cursors->size();
  size_t final_position;  // From cursors->begin().
  if (direction == Direction::kForwards) {
    final_position = (index + repetitions) % cursors->size();
  } else if (index >= repetitions) {
    final_position = index - repetitions;
  } else {
    final_position = cursors->size() - (repetitions - index);
  }
  output = cursors->begin();
  std::advance(output, final_position);
  return *output;
}

void OpenBuffer::DestroyCursor() {
  auto cursors = active_cursors();
  if (cursors->size() <= 1) {
    return;
  }
  size_t repetitions = min(options_.editor->modifiers().repetitions.value_or(1),
                           cursors->size() - 1);
  for (size_t i = 0; i < repetitions; i++) {
    cursors->DeleteCurrentCursor();
  }
  CHECK_LE(position().line, LineNumber(0) + contents_.size());
}

void OpenBuffer::DestroyOtherCursors() {
  CheckPosition();
  auto position = this->position();
  CHECK_LE(position, LineColumn(LineNumber(0) + contents_.size()));
  auto cursors = active_cursors();
  cursors->clear();
  cursors->insert(position);
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
    modifiers.structure->SeekToNext(this, forward, &output.begin);
  }
  switch (modifiers.boundary_begin) {
    case Modifiers::CURRENT_POSITION:
      output.begin = modifiers.direction == Direction::kForwards
                         ? max(position, output.begin)
                         : min(position, output.begin);
      break;

    case Modifiers::LIMIT_CURRENT: {
      if (modifiers.structure->SeekToLimit(this, backward, &output.begin)) {
        Seek(contents_, &output.begin)
            .WrappingLines()
            .WithDirection(forward)
            .Once();
      }
    } break;

    case Modifiers::LIMIT_NEIGHBOR:
      if (modifiers.structure->SeekToLimit(this, backward, &output.begin)) {
        modifiers.structure->SeekToNext(this, backward, &output.begin);
        modifiers.structure->SeekToLimit(this, forward, &output.begin);
      }
  }
  LOG(INFO) << "After seek, initial position: " << output.begin;
  output.end = modifiers.direction == Direction::kForwards
                   ? max(position, output.begin)
                   : min(position, output.begin);
  bool move_start = true;
  for (size_t i = 1; i < modifiers.repetitions.value_or(1); i++) {
    LineColumn position = output.end;
    if (!modifiers.structure->SeekToLimit(this, forward, &output.end)) {
      move_start = false;
      break;
    }
    modifiers.structure->SeekToNext(this, forward, &output.end);
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
          modifiers.structure->SeekToLimit(this, forward, &output.end);
      break;

    case Modifiers::LIMIT_NEIGHBOR:
      move_start &=
          modifiers.structure->SeekToLimit(this, forward, &output.end);
      modifiers.structure->SeekToNext(this, forward, &output.end);
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

const ParseTree* OpenBuffer::current_tree(const ParseTree* root) const {
  CHECK(root);
  auto route = FindRouteToPosition(*root, position());
  if (route.size() < tree_depth_) {
    return root;
  }
  if (route.size() > tree_depth_) {
    route.resize(tree_depth_);
  }
  return FollowRoute(*root, route);
}

std::shared_ptr<const ParseTree> OpenBuffer::current_zoomed_out_parse_tree(
    LineNumberDelta view_size) const {
  auto simplified_tree = simplified_parse_tree();
  auto it = zoomed_out_parse_trees_.find(view_size);
  if (it == zoomed_out_parse_trees_.end() ||
      it->second.simplified_parse_tree != simplified_tree) {
    syntax_data_
        .Run([lines_size = lines_size(), view_size,
              simplified_tree]() -> std::shared_ptr<ParseTree> {
          static Tracker tracker(
              L"OpenBuffer::current_zoomed_out_parse_tree::produce");
          auto tracker_call = tracker.Call();
          return std::make_shared<ParseTree>(
              ZoomOutTree(*simplified_tree, lines_size, view_size));
        })
        .SetConsumer([this, view_size, simplified_tree](
                         std::shared_ptr<ParseTree> zoomed_parse_tree) mutable {
          if (simplified_parse_tree() != simplified_tree) {
            LOG(INFO) << "Parse tree changed in the meantime, discarding.";
            return;
          }
          LOG(INFO) << "Installing tree.";
          zoomed_out_parse_trees_[view_size] = {
              .simplified_parse_tree = std::move(simplified_tree),
              .zoomed_out_parse_tree = std::move(zoomed_parse_tree)};
        });
  }
  // We don't check if it's still current: we prefer returning a stale tree over
  // an empty tree. The empty tree would just cause flickering as the user is
  // typing; the stale tree is almost always correct (and, when it isn't, it'll
  // be refreshed very shortly).
  return it != zoomed_out_parse_trees_.end()
             ? it->second.zoomed_out_parse_tree
             : std::make_shared<ParseTree>(Range());
}

std::unique_ptr<BufferTerminal> OpenBuffer::NewTerminal() {
  return std::make_unique<BufferTerminal>(this, &contents_);
}

const std::shared_ptr<const Line> OpenBuffer::current_line() const {
  return LineAt(position().line);
}

std::shared_ptr<const Line> OpenBuffer::LineAt(LineNumber line_number) const {
  if (line_number > contents_.EndLine()) {
    return nullptr;
  }
  return contents_.at(line_number);
}

std::shared_ptr<OpenBuffer> OpenBuffer::GetBufferFromCurrentLine() {
  if (current_line() == nullptr) {
    return nullptr;
  }
  auto target = current_line()->environment()->Lookup(
      Environment::Namespace(), L"buffer",
      vm::VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype);
  if (target == nullptr) {
    return nullptr;
  }
  return std::static_pointer_cast<OpenBuffer>(target->user_value);
}

wstring OpenBuffer::ToString() const { return contents_.ToString(); }

const struct timespec OpenBuffer::time_last_exit() const {
  return time_last_exit_;
}

void OpenBuffer::PushSignal(int sig) {
  switch (sig) {
    case SIGINT:
      if (terminal_ == nullptr ? child_pid_ == -1 : fd_ == nullptr) {
        status_.SetWarningText(L"No subprocess found.");
      } else if (terminal_ == nullptr) {
        status_.SetInformationText(L"SIGINT >> pid:" + to_wstring(child_pid_));
        kill(child_pid_, sig);
      } else {
        string sequence(1, 0x03);
        (void)write(fd_->fd(), sequence.c_str(), sequence.size());
        status_.SetInformationText(L"SIGINT");
      }
      break;

    case SIGTSTP:
      static const string sequence(1, 0x1a);
      if (terminal_ != nullptr && fd_ != nullptr) {
        (void)write(fd_->fd(), sequence.c_str(), sequence.size());
      }
      break;

    default:
      status_.SetWarningText(L"Unexpected signal received: " + to_wstring(sig));
  }
}

Viewers* OpenBuffer::viewers() { return &viewers_; }
const Viewers* OpenBuffer::viewers() const { return &viewers_; }

FileSystemDriver* OpenBuffer::file_system_driver() {
  return &file_system_driver_;
}

futures::Value<std::wstring> OpenBuffer::TransformKeyboardText(
    std::wstring input) {
  using afc::vm::VMType;
  auto input_shared = std::make_shared<std::wstring>(std::move(input));
  return futures::Transform(
      futures::ForEach(
          keyboard_text_transformers_.begin(),
          keyboard_text_transformers_.end(),
          [this, input_shared](const std::unique_ptr<Value>& t) {
            CHECK(t != nullptr);
            std::vector<Value::Ptr> args;
            args.push_back(Value::NewString(std::move(*input_shared)));
            return futures::Transform(
                Call(*t, std::move(args),
                     [work_queue =
                          work_queue()](std::function<void()> callback) {
                       work_queue->Schedule(std::move(callback));
                     }),
                [input_shared](const std::unique_ptr<Value>& value) {
                  CHECK(value != nullptr);
                  CHECK(value->IsString());
                  *input_shared = std::move(value->str);
                  return IterationControlCommand::kContinue;
                });
          }),
      [input_shared](IterationControlCommand) {
        return futures::Past(std::move(*input_shared));
      });
}

bool OpenBuffer::AddKeyboardTextTransformer(unique_ptr<Value> transformer) {
  if (transformer == nullptr || transformer->type.type != VMType::FUNCTION ||
      transformer->type.type_arguments.size() != 2 ||
      transformer->type.type_arguments[0].type != VMType::VM_STRING ||
      transformer->type.type_arguments[1].type != VMType::VM_STRING) {
    status_.SetWarningText(
        L": Unexpected type for keyboard text transformer: " +
        transformer->type.ToString());
    return false;
  }
  keyboard_text_transformers_.push_back(std::move(transformer));
  return true;
}

void OpenBuffer::SetInputFiles(int input_fd, int input_error_fd,
                               bool fd_is_terminal, pid_t child_pid) {
  if (Read(buffer_variables::clear_on_reload)) {
    ClearContents(BufferContents::CursorsBehavior::kUnmodified);
    SetDiskState(DiskState::kCurrent);
  }

  CHECK_EQ(child_pid_, -1);
  terminal_ = fd_is_terminal ? NewTerminal() : nullptr;

  auto new_reader = [this](int fd, LineModifierSet modifiers)
      -> std::unique_ptr<FileDescriptorReader> {
    if (fd == -1) {
      return nullptr;
    }
    FileDescriptorReader::Options options;
    options.buffer = this;
    options.terminal = terminal_.get();
    options.fd = fd;
    options.modifiers = std::move(modifiers);
    options.read_evaluator = &async_read_evaluator_;
    return std::make_unique<FileDescriptorReader>(std::move(options));
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

LineColumn OpenBuffer::PositionBefore(LineColumn position) const {
  if (position.column > ColumnNumber(0)) {
    position.column--;
  } else if (position.line > LineNumber(0)) {
    position.line = min(position.line, LineNumber(0) + contents_.size()) -
                    LineNumberDelta(1);
    position.column = contents_.at(position.line)->EndColumn();
  }
  return position;
}

const LineColumn OpenBuffer::position() const {
  return cursors_tracker_.position();
}

void OpenBuffer::set_position(const LineColumn& position) {
  set_current_cursor(position);
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
        SetDiskState(*old_state);
        delete old_state;
      });
}

bool OpenBuffer::dirty() const {
  return (disk_state_ == DiskState::kStale &&
          (!Read(buffer_variables::path).empty() ||
           !contents()->EveryLine(
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
  Set(variable, editor()->modifiers().repetitions.has_value()
                    ? editor()->modifiers().repetitions != 0
                    : !Read(variable));
}

const wstring& OpenBuffer::Read(const EdgeVariable<wstring>* variable) const {
  return string_variables_.Get(variable);
}

void OpenBuffer::Set(const EdgeVariable<wstring>* variable, wstring value) {
  string_variables_.Set(variable, value);

  // TODO: This should be in the variable definition, not here. Ugh.
  if (variable == buffer_variables::symbol_characters ||
      variable == buffer_variables::tree_parser ||
      variable == buffer_variables::language_keywords ||
      variable == buffer_variables::typos) {
    UpdateTreeParser();
  }
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

std::vector<Path> GetPathsWithExtensionsForContext(const OpenBuffer& buffer,
                                                   const Path& original_path) {
  std::vector<Path> output = {original_path};
  auto extensions = TokenizeBySpaces(
      *NewLazyString(buffer.Read(buffer_variables::file_context_extensions)));
  for (auto& extension_token : extensions) {
    CHECK(!extension_token.value.empty());
    output.push_back(Path::WithExtension(original_path, extension_token.value));
  }
  return output;
}

void StartAdjustingStatusContext(std::shared_ptr<OpenBuffer> buffer) {
  auto adjusted_position = buffer->AdjustLineColumn(buffer->position());
  std::wstring line = GetCurrentToken(
      {.contents = buffer->contents(),
       .line_column = adjusted_position,
       .token_characters = buffer->Read(buffer_variables::path_characters)});
  if (line.find_first_not_of(L"/.") == wstring::npos) {
    // If there are only slashes or dots, it's probably not very useful to show
    // the contents of this path.
    buffer->status()->set_context(nullptr);
    return;
  }
  auto path = Path::FromString(line);
  if (path.IsError()) {
    buffer->status()->set_context(nullptr);
    return;
  }

  // When the cursor moves quickly, there's a race between multiple executions
  // of this logic. To avoid this, each call captures the original position and
  // uses that to avoid taking any effects when the position changes in the
  // meantime.
  auto paths = GetPathsWithExtensionsForContext(*buffer, path.value());
  futures::ForEachWithCopy(
      paths.begin(), paths.end(),
      [buffer, adjusted_position](Path path) {
        return buffer->file_system_driver()
            ->Stat(path)
            .Transform([buffer, path, adjusted_position](struct stat s) {
              if (S_ISSOCK(s.st_mode) || S_ISBLK(s.st_mode) ||
                  S_ISCHR(s.st_mode) || S_ISFIFO(s.st_mode)) {
                return futures::Past(
                    Success(futures::IterationControlCommand::kContinue));
              }
              if (adjusted_position !=
                  buffer->AdjustLineColumn(buffer->position())) {
                return futures::Past(
                    Success(futures::IterationControlCommand::kStop));
              }
              OpenFileOptions options;
              options.editor_state = buffer->editor();
              options.path = path;
              options.ignore_if_not_found = true;
              options.insertion_type = BuffersList::AddBufferType::kIgnore;
              options.use_search_paths = false;
              return OpenFile(std::move(options))
                  .Transform([buffer, adjusted_position](
                                 map<wstring, shared_ptr<OpenBuffer>>::iterator
                                     buffer_context_it) {
                    if (adjusted_position ==
                        buffer->AdjustLineColumn(buffer->position())) {
                      buffer->status()->set_context(
                          buffer_context_it ==
                                  buffer->editor()->buffers()->end()
                              ? nullptr
                              : buffer_context_it->second);
                    }
                    return Success(futures::IterationControlCommand::kStop);
                  });
            })
            .ConsumeErrors([](Error) {
              return futures::IterationControlCommand::kContinue;
            });
      })
      .Transform([buffer,
                  adjusted_position](futures::IterationControlCommand result) {
        if (result == futures::IterationControlCommand::kContinue &&
            adjusted_position == buffer->AdjustLineColumn(buffer->position())) {
          buffer->status()->set_context(nullptr);
        }
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
    CHECK(last_transformation_stack_.back() != nullptr);
    last_transformation_stack_.back()->PushBack(transformation);
    CHECK(!undo_past_.empty());
  } else {
    undo_past_.push_back(std::make_unique<transformation::Stack>());
  }

  undo_past_.back()->PushFront(transformation::Cursors{
      .cursors = *active_cursors(), .active = position()});

  std::optional<futures::Value<EmptyValue>> transformation_result;
  if (cursors_affected == Modifiers::CursorsAffected::kAll) {
    CursorsSet single_cursor;
    CursorsSet* cursors = active_cursors();
    CHECK(cursors != nullptr);
    transformation_result = cursors_tracker_.ApplyTransformationToCursors(
        cursors, [this, transformation = std::move(transformation),
                  mode](LineColumn position) {
          return futures::Transform(
              Apply(transformation, position, mode),
              [](transformation::Result result) { return result.position; });
        });
  } else {
    VLOG(6) << "Adjusting default cursor (!multiple_cursors).";
    transformation_result = futures::Transform(
        Apply(std::move(transformation), position(), mode),
        [this](const transformation::Result& result) {
          active_cursors()->MoveCurrentCursor(result.position);
          return EmptyValue();
        });
  }
  return futures::Transform(transformation_result.value(),
                            [shared_this = shared_from_this()](EmptyValue) {
                              // This proceeds in the background but we can only
                              // start it once the transformation is evaluated
                              // (since we don't know the cursor position
                              // otherwise).
                              StartAdjustingStatusContext(shared_this);
                              return EmptyValue();
                            });
}

futures::Value<typename transformation::Result> OpenBuffer::Apply(
    transformation::Variant transformation, LineColumn position,
    transformation::Input::Mode mode) {
  transformation::Input input(this);
  input.mode = mode;
  input.position = position;
  if (Read(buffer_variables::delete_into_paste_buffer)) {
    auto it = editor()->buffers()->insert({kFuturePasteBuffer, nullptr});
    if (it.first->second == nullptr) {
      LOG(INFO) << "Creating paste buffer.";
      it.first->second =
          OpenBuffer::New({.editor = editor(), .name = kFuturePasteBuffer});
    }
    input.delete_buffer = it.first->second.get();
    CHECK(input.delete_buffer != nullptr);
  } else {
    input.delete_buffer = nullptr;
  }

  auto inner_future = transformation::Apply(transformation, std::move(input));
  return futures::Transform(
      inner_future, [this, transformation = std::move(transformation),
                     mode](transformation::Result result) {
        if (mode == transformation::Input::Mode::kFinal &&
            Read(buffer_variables::delete_into_paste_buffer)) {
          if (!result.added_to_paste_buffer) {
            editor()->buffers()->erase(kFuturePasteBuffer);
          } else if (auto paste_buffer =
                         editor()->buffers()->find(kFuturePasteBuffer);
                     paste_buffer != editor()->buffers()->end()) {
            editor()
                ->buffers()
                ->insert({OpenBuffer::kPasteBuffer, nullptr})
                .first->second = paste_buffer->second;
          }
        }

        if (result.modified_buffer &&
            mode == transformation::Input::Mode::kFinal) {
          editor()->StartHandlingInterrupts();
          last_transformation_ = std::move(transformation);
        }

        CHECK(!undo_past_.empty());
        undo_past_.back()->PushFront(
            transformation::Stack{.stack = result.undo_stack->stack});
        return result;
      });
}

futures::Value<EmptyValue> OpenBuffer::RepeatLastTransformation() {
  size_t repetitions = options_.editor->repetitions().value_or(1);
  options_.editor->ResetRepetitions();
  return ApplyToCursors(transformation::Repetitions{
      repetitions,
      std::make_shared<transformation::Variant>(last_transformation_)});
}

void OpenBuffer::PushTransformationStack() {
  if (last_transformation_stack_.empty()) {
    undo_past_.push_back(std::make_unique<transformation::Stack>());
  }
  last_transformation_stack_.emplace_back(
      std::make_unique<transformation::Stack>());
}

void OpenBuffer::PopTransformationStack() {
  CHECK(!last_transformation_stack_.empty());
  last_transformation_ = std::move(*last_transformation_stack_.back());
  last_transformation_stack_.pop_back();
  if (!last_transformation_stack_.empty()) {
    last_transformation_stack_.back()->PushBack(last_transformation_);
  }
}

futures::Value<EmptyValue> OpenBuffer::Undo(UndoMode undo_mode) {
  struct Data {
    std::list<std::unique_ptr<transformation::Stack>>* source;
    std::list<std::unique_ptr<transformation::Stack>>* target;
    size_t repetitions = 0;
  };
  auto data = std::make_shared<Data>();
  if (editor()->direction() == Direction::kForwards) {
    data->source = &undo_past_;
    data->target = &undo_future_;
  } else {
    data->source = &undo_future_;
    data->target = &undo_past_;
  }
  return futures::Transform(
      futures::While([this, undo_mode, data] {
        if (data->repetitions == editor()->repetitions().value_or(1) ||
            data->source->empty()) {
          return futures::Past(IterationControlCommand::kStop);
        }
        transformation::Input input(this);
        input.position = position();
        // We've undone the entire changes, so...
        last_transformation_stack_.clear();
        return futures::Transform(
            transformation::Apply(*data->source->back(), input),
            [this, undo_mode, data](transformation::Result result) {
              data->target->push_back(std::move(result.undo_stack));
              data->source->pop_back();
              if (result.modified_buffer ||
                  undo_mode == OpenBuffer::UndoMode::kOnlyOne) {
                data->repetitions++;
              }
              return IterationControlCommand::kContinue;
            });
      }),
      [shared_this = shared_from_this()](IterationControlCommand) {
        StartAdjustingStatusContext(shared_this);
        return EmptyValue();
      });
}

void OpenBuffer::set_filter(unique_ptr<Value> filter) {
  filter_ = std::move(filter);
  filter_version_++;
}

const multimap<size_t, LineMarks::Mark>* OpenBuffer::GetLineMarks() const {
  auto marks = editor()->line_marks();
  if (marks->updates > line_marks_last_updates_) {
    LOG(INFO) << Read(buffer_variables::name) << ": Updating marks.";
    line_marks_.clear();
    auto relevant_marks =
        marks->GetMarksForTargetBuffer(Read(buffer_variables::name));
    for (auto& mark : relevant_marks) {
      line_marks_.insert(make_pair(mark.target.line.line, mark));
    }
    line_marks_last_updates_ = marks->updates;
  }
  return &line_marks_;
}

wstring OpenBuffer::GetLineMarksText() const {
  const auto* marks = GetLineMarks();
  wstring output;
  if (!marks->empty()) {
    size_t expired_marks = 0;
    for (const auto& mark : *marks) {
      if (mark.second.IsExpired()) {
        expired_marks++;
      }
    }
    CHECK_LE(expired_marks, marks->size());
    output = L"marks:" + to_wstring(marks->size() - expired_marks);
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

void OpenBuffer::ReadData(std::unique_ptr<FileDescriptorReader>* source) {
  CHECK(source != nullptr);
  CHECK(*source != nullptr);
  (*source)->ReadData().SetConsumer(
      [this, source](FileDescriptorReader::ReadResult value) {
        if (value != FileDescriptorReader::ReadResult::kDone) return;
        RegisterProgress();
        (*source) = nullptr;
        if (fd_ == nullptr && fd_error_ == nullptr) {
          EndOfFile();
        }
      });
}

}  // namespace editor
}  // namespace afc
