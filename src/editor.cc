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
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/language/wstring.h"
#include "src/open_file_command.h"
#include "src/run_command_handler.h"
#include "src/server.h"
#include "src/set_buffer_mode.h"
#include "src/set_variable_command.h"
#include "src/shapes.h"
#include "src/substring.h"
#include "src/terminal.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm_transformation.h"
#include "src/widget_list.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<editor::EditorState*> {
  static editor::EditorState* get(Value* value) {
    return static_cast<editor::EditorState*>(value->user_value.get());
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::EditorState*>::vmtype =
    VMType::ObjectType(L"Editor");
}  // namespace vm
namespace editor {
namespace {
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
using language::MakeNonNullUnique;
using language::NonNull;
using language::Observers;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using std::wstring;

template <typename MethodReturnType>
void RegisterBufferMethod(ObjectType* editor_type, const wstring& name,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  auto callback = MakeNonNullUnique<Value>(VMType::FUNCTION);
  // Returns nothing.
  callback->type.type_arguments = {VMType(VMType::VM_VOID),
                                   VMType::ObjectType(editor_type)};
  callback->callback =
      [method](std::vector<NonNull<std::unique_ptr<Value>>> args, Trampoline&) {
        CHECK_EQ(args.size(), size_t(1));
        CHECK_EQ(args[0]->type, VMType::ObjectType(L"Editor"));

        auto editor = static_cast<EditorState*>(args[0]->user_value.get());
        CHECK(editor != nullptr);
        return editor
            ->ForEachActiveBuffer([method](OpenBuffer& buffer) {
              (buffer.*method)();
              return futures::Past(EmptyValue());
            })
            .Transform([editor](EmptyValue) {
              editor->ResetModifiers();
              return EvaluationOutput::New(Value::NewVoid());
            });
      };
  editor_type->AddField(name, std::move(callback));
}
}  // namespace

// Executes pending work from all buffers.
void EditorState::ExecutePendingWork() { work_queue_->Execute(); }

std::optional<struct timespec> EditorState::WorkQueueNextExecution() const {
  std::optional<struct timespec> output;
  for (auto& buffer : buffers_) {
    if (auto buffer_output = buffer.second->work_queue()->NextExecution();
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

template <typename EdgeStruct, typename FieldValue>
void RegisterVariableFields(
    EdgeStruct* edge_struct, afc::vm::ObjectType* editor_type,
    const FieldValue& (EditorState::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (EditorState::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    editor_type->AddField(
        variable->name(),
        vm::NewCallback([reader, variable](EditorState* editor) {
          return (editor->*reader)(variable);
        }));

    // Setter.
    editor_type->AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            [variable, setter](EditorState* editor, FieldValue value) {
              (editor->*setter)(variable, value);
            }));
  }
}

std::shared_ptr<Environment> EditorState::BuildEditorEnvironment() {
  auto environment =
      std::make_shared<Environment>(afc::vm::Environment::GetDefault());

  environment->Define(L"terminal_backspace",
                      Value::NewString({Terminal::BACKSPACE}));
  environment->Define(L"terminal_control_a",
                      Value::NewString({Terminal::CTRL_A}));
  environment->Define(L"terminal_control_e",
                      Value::NewString({Terminal::CTRL_E}));
  environment->Define(L"terminal_control_d",
                      Value::NewString({Terminal::CTRL_D}));
  environment->Define(L"terminal_control_k",
                      Value::NewString({Terminal::CTRL_K}));
  environment->Define(L"terminal_control_u",
                      Value::NewString({Terminal::CTRL_U}));

  auto editor_type = MakeNonNullUnique<ObjectType>(L"Editor");

  // Methods for Editor.
  RegisterVariableFields<EdgeStruct<bool>, bool>(
      editor_variables::BoolStruct(), editor_type.get(), &EditorState::Read,
      &EditorState::Set);

  RegisterVariableFields<EdgeStruct<wstring>, wstring>(
      editor_variables::StringStruct(), editor_type.get(), &EditorState::Read,
      &EditorState::Set);

  RegisterVariableFields<EdgeStruct<int>, int>(
      editor_variables::IntStruct(), editor_type.get(), &EditorState::Read,
      &EditorState::Set);

  editor_type->AddField(
      L"EnterSetBufferMode", vm::NewCallback([](EditorState* editor) {
        CHECK(editor != nullptr);
        editor->set_keyboard_redirect(NewSetBufferMode(*editor));
      }));

  editor_type->AddField(L"SetActiveBuffer",
                        vm::NewCallback([](EditorState* editor, int delta) {
                          editor->SetActiveBuffer(delta);
                        }));

  editor_type->AddField(L"AdvanceActiveBuffer",
                        vm::NewCallback([](EditorState* editor, int delta) {
                          editor->AdvanceActiveBuffer(delta);
                        }));

  editor_type->AddField(L"ZoomToLeaf", vm::NewCallback([](EditorState*) {}));

  editor_type->AddField(
      L"SetVariablePrompt",
      vm::NewCallback([](EditorState* editor, std::wstring variable) {
        CHECK(editor != nullptr);
        SetVariableCommandHandler(variable, *editor);
      }));

  editor_type->AddField(L"home", vm::NewCallback([](EditorState* editor) {
                          return editor->home_directory().read();
                        }));

  editor_type->AddField(
      L"pop_repetitions", vm::NewCallback([](EditorState* editor) {
        auto value = static_cast<int>(editor->repetitions().value_or(1));
        editor->ResetRepetitions();
        return value;
      }));

  editor_type->AddField(
      L"ForEachActiveBuffer",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype})},
          [](std::vector<NonNull<std::unique_ptr<Value>>> input,
             Trampoline& trampoline) {
            EditorState* editor =
                VMTypeMapper<EditorState*>::get(input[0].get());
            NonNull<std::shared_ptr<PossibleError>> output;
            return editor
                ->ForEachActiveBuffer([callback = std::move(input[1]->callback),
                                       &trampoline,
                                       output](OpenBuffer& buffer) {
                  std::vector<NonNull<std::unique_ptr<Value>>> args;
                  args.push_back(
                      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
                          buffer.shared_from_this()));
                  return callback(std::move(args), trampoline)
                      .Transform([](EvaluationOutput) { return Success(); })
                      .ConsumeErrors([output](Error error) {
                        *output = error;
                        return futures::Past(EmptyValue());
                      });
                })
                .Transform(
                    [output](EmptyValue) -> ValueOrError<EvaluationOutput> {
                      if (output->IsError()) return output->error();
                      return EvaluationOutput::Return(Value::NewVoid());
                    });
          }));

  editor_type->AddField(
      L"ForEachActiveBufferWithRepetitions",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype})},
          [](std::vector<NonNull<std::unique_ptr<Value>>> input,
             Trampoline& trampoline) {
            EditorState* editor =
                VMTypeMapper<EditorState*>::get(input[0].get());
            return editor
                ->ForEachActiveBufferWithRepetitions([callback = std::move(
                                                          input[1]->callback),
                                                      &trampoline](
                                                         OpenBuffer& buffer) {
                  std::vector<NonNull<std::unique_ptr<Value>>> args;
                  args.push_back(
                      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
                          buffer.shared_from_this()));
                  return callback(std::move(args), trampoline)
                      .Transform([](EvaluationOutput) { return Success(); })
                      // TODO(easy): Don't ConsumeErrors; change
                      // ForEachActiveBuffer.
                      .ConsumeErrors(
                          [](Error) { return futures::Past(EmptyValue()); });
                })
                .Transform([](EmptyValue) {
                  return EvaluationOutput::Return(Value::NewVoid());
                });
          }));

  editor_type->AddField(L"ProcessInput",
                        vm::NewCallback([](EditorState* editor, int c) {
                          CHECK(editor != nullptr);
                          editor->ProcessInput(c);
                        }));

  editor_type->AddField(
      L"ConnectTo",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::VM_STRING},
          [](std::vector<NonNull<std::unique_ptr<Value>>> args,
             Trampoline&) -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMTypeMapper<EditorState*>::vmtype);
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);
            auto target_path = Path::FromString(args[1]->str);
            if (target_path.IsError()) {
              editor->status().SetWarningText(L"ConnectTo error: " +
                                              target_path.error().description);
              return futures::Past(target_path.error());
            }
            OpenServerBuffer(*editor, target_path.value());
            return futures::Past(EvaluationOutput::Return(Value::NewVoid()));
          }));

  editor_type->AddField(
      L"WaitForClose",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::ObjectType(L"SetString")},
          [](std::vector<NonNull<std::unique_ptr<Value>>> args, Trampoline&) {
            CHECK_EQ(args.size(), 2u);
            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            const auto& buffers_to_wait =
                *static_cast<std::set<wstring>*>(args[1]->user_value.get());

            auto values =
                std::make_shared<std::vector<futures::Value<EmptyValue>>>();
            for (const auto& buffer_name_str : buffers_to_wait) {
              if (auto buffer_it =
                      editor->buffers()->find(BufferName(buffer_name_str));
                  buffer_it != editor->buffers()->end()) {
                CHECK(buffer_it->second != nullptr);
                values->push_back(buffer_it->second->NewCloseFuture());
              }
            }
            return futures::ForEach(
                       values,
                       [values](futures::Value<EmptyValue>& future) {
                         return future.Transform([](EmptyValue) {
                           return futures::IterationControlCommand::kContinue;
                         });
                       })
                .Transform([](futures::IterationControlCommand) {
                  return EvaluationOutput::Return(Value::NewVoid());
                });
          }));

  editor_type->AddField(L"SendExitTo",
                        vm::NewCallback([](EditorState*, wstring args) {
                          int fd = open(ToByteString(args).c_str(), O_WRONLY);
                          string command = "editor.Exit(0);\n";
                          write(fd, command.c_str(), command.size());
                          close(fd);
                        }));

  editor_type->AddField(L"Exit", vm::NewCallback([](EditorState*, int status) {
                          LOG(INFO) << "Exit: " << status;
                          exit(status);
                        }));

  editor_type->AddField(L"SetStatus",
                        vm::NewCallback([](EditorState* editor, wstring s) {
                          editor->status_.SetInformationText(s);
                        }));

  editor_type->AddField(L"PromptAndOpenFile",
                        vm::NewCallback([](EditorState* editor) {
                          CHECK(editor != nullptr);
                          NewOpenFileCommand(*editor)->ProcessInput(0);
                        }));

  editor_type->AddField(L"set_screen_needs_hard_redraw",
                        vm::NewCallback([](EditorState* editor, bool value) {
                          editor->set_screen_needs_hard_redraw(value);
                        }));

  editor_type->AddField(
      L"set_exit_value",
      vm::NewCallback([](EditorState* editor, int exit_value) {
        editor->exit_value_ = exit_value;
      }));

  editor_type->AddField(
      L"ForkCommand",
      vm::NewCallback([](EditorState* editor, ForkCommandOptions* options) {
        CHECK(editor != nullptr);
        return std::move(ForkCommand(*editor, *options).get_shared());
      }));

  editor_type->AddField(
      L"repetitions", vm::NewCallback([](EditorState* editor) {
        // TODO: Somehow expose the optional to the VM.
        return static_cast<int>(editor->repetitions().value_or(1));
      }));

  editor_type->AddField(L"set_repetitions",
                        vm::NewCallback([](EditorState* editor, int times) {
                          editor->set_repetitions(times);
                        }));

  editor_type->AddField(
      L"OpenFile",
      Value::NewFunction(
          {VMType::ObjectType(L"Buffer"), VMTypeMapper<EditorState*>::vmtype,
           VMType::VM_STRING, VMType::VM_BOOLEAN},
          [](std::vector<NonNull<std::unique_ptr<Value>>> args, Trampoline&) {
            CHECK_EQ(args.size(), 3u);
            CHECK_EQ(args[0]->type, VMTypeMapper<EditorState*>::vmtype);
            auto editor_state =
                static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(args[1]->IsString());
            CHECK(args[2]->IsBool());
            CHECK(editor_state != nullptr);
            return OpenFile(
                       OpenFileOptions{
                           .editor_state = *editor_state,
                           .path = Path::FromString(args[1]->str).AsOptional(),
                           .insertion_type =
                               args[2]->boolean
                                   ? BuffersList::AddBufferType::kVisit
                                   : BuffersList::AddBufferType::kIgnore})
                .Transform(
                    [](std::map<BufferName,
                                std::shared_ptr<OpenBuffer>>::iterator result) {
                      return EvaluationOutput::Return(
                          Value::NewObject(L"Buffer", result->second));
                    });
          }));

  editor_type->AddField(
      L"AddBinding",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype, VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [](std::vector<NonNull<std::unique_ptr<Value>>> args) {
            CHECK_EQ(args.size(), 4u);
            CHECK_EQ(args[0]->type, VMTypeMapper<EditorState*>::vmtype);
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            CHECK_EQ(args[2]->type, VMType::VM_STRING);
            EditorState* editor =
                static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);
            editor->default_commands_->Add(args[1]->str, args[2]->str,
                                           std::move(args[3]),
                                           editor->environment_);
            return Value::NewVoid();
          }));

  RegisterBufferMethod(editor_type.get(), L"ToggleActiveCursors",
                       &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"PushActiveCursors",
                       &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"PopActiveCursors",
                       &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(editor_type.get(), L"SetActiveCursorsToMarks",
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(editor_type.get(), L"CreateCursor",
                       &OpenBuffer::CreateCursor);
  RegisterBufferMethod(editor_type.get(), L"DestroyCursor",
                       &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(editor_type.get(), L"DestroyOtherCursors",
                       &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(editor_type.get(), L"RepeatLastTransformation",
                       &OpenBuffer::RepeatLastTransformation);
  environment->DefineType(L"Editor", std::move(editor_type));

  environment->Define(
      L"editor",
      Value::NewObject(L"Editor", shared_ptr<void>(this, [](void*) {})));

  OpenBuffer::RegisterBufferType(*this, environment.get());

  InitShapes(environment.get());
  RegisterTransformations(this, environment.get());
  Modifiers::Register(environment.get());
  ForkCommandOptions::Register(environment.get());
  LineColumn::Register(environment.get());
  Range::Register(environment.get());
  return environment;
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
      environment_(BuildEditorEnvironment()),
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
      status_(GetConsole(), audio_player_) {
  work_queue_->OnSchedule().Add([this] {
    NotifyInternalEvent();
    return Observers::State::kAlive;
  });
  auto paths = edge_path();
  futures::ForEach(paths.begin(), paths.end(), [this](Path dir) {
    auto path = Path::Join(dir, Path::FromString(L"hooks/start.cc").value());
    ValueOrError<NonNull<std::unique_ptr<Expression>>> expression =
        CompileFile(ToByteString(path.read()), environment_);
    if (expression.IsError()) {
      Error error =
          Error::Augment(path.read() + L": error: ", expression.error());
      LOG(INFO) << "Compilation error: " << error;
      status_.SetWarningText(error.description);
      return futures::Past(futures::IterationControlCommand::kContinue);
    }
    LOG(INFO) << "Evaluating file: " << path;
    return Evaluate(
               *expression.value(), environment_,
               [path, work_queue = work_queue()](std::function<void()> resume) {
                 LOG(INFO) << "Evaluation of file yields: " << path;
                 work_queue->Schedule(std::move(resume));
               })
        .Transform([](NonNull<std::unique_ptr<Value>>) {
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
    buffer.second->Close();
  }

  environment_->Clear();  // We may have loops. This helps break them.
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
  if (auto buffer = buffer_tree_.active_buffer(); buffer != nullptr) {
    buffer->CheckPosition();
  }
}

void EditorState::CloseBuffer(OpenBuffer& buffer) {
  buffer.PrepareToClose().SetConsumer(
      [this, buffer = buffer.shared_from_this()](PossibleError error) {
        if (error.IsError()) {
          buffer->status().SetWarningText(
              L"🖝  Unable to close (“*ad” to ignore): " +
              error.error().description + L": " +
              buffer->Read(buffer_variables::name));
          return;
        }

        buffer->Close();
        buffer_tree_.RemoveBuffer(*buffer);
        buffers_.erase(buffer->name());
        AdjustWidgets();
      });
}

void EditorState::set_current_buffer(std::shared_ptr<OpenBuffer> buffer,
                                     CommandArgumentModeApplyMode apply_mode) {
  if (buffer != nullptr) {
    if (apply_mode == CommandArgumentModeApplyMode::kFinal) {
      buffer->Visit();
    } else {
      buffer->Enter();
    }
  }
  buffer_tree_.GetActiveLeaf()->SetBuffer(buffer);
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
  return current_buffer() != nullptr;
}
shared_ptr<OpenBuffer> EditorState::current_buffer() {
  return buffer_tree_.active_buffer();
}
const shared_ptr<OpenBuffer> EditorState::current_buffer() const {
  return buffer_tree_.active_buffer();
}

std::vector<std::shared_ptr<OpenBuffer>> EditorState::active_buffers() const {
  std::vector<std::shared_ptr<OpenBuffer>> output;
  if (status().GetType() == Status::Type::kPrompt) {
    output.push_back(status().prompt_buffer());
  } else if (Read(editor_variables::multiple_buffers)) {
    output = buffer_tree_.GetAllBuffers();
  } else if (auto buffer = current_buffer(); buffer != nullptr) {
    if (buffer->status().GetType() == Status::Type::kPrompt) {
      buffer = buffer->status().prompt_buffer();
    }
    output.push_back(buffer);
  }
  return output;
}

void EditorState::AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                            BuffersList::AddBufferType insertion_type) {
  auto initial_active_buffers = active_buffers();
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
             [callback](const std::shared_ptr<OpenBuffer>& buffer) {
               return callback(*buffer).Transform([](EmptyValue) {
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
  } else if (auto buffer = buffer_tree().GetBuffer(
                 (max(modifiers().repetitions.value(), 1ul) - 1) %
                 buffer_tree().BuffersCount());
             buffer != nullptr) {
    value = callback(*buffer);
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

void EditorState::Terminate(TerminationType termination_type, int exit_value) {
  status().SetInformationText(L"Exit: Preparing to close buffers (" +
                              std::to_wstring(buffers_.size()) + L")");
  if (termination_type == TerminationType::kWhenClean) {
    LOG(INFO) << "Checking buffers for termination.";
    std::vector<wstring> buffers_with_problems;
    for (auto& it : buffers_) {
      if (auto result = it.second->IsUnableToPrepareToClose();
          result.IsError()) {
        buffers_with_problems.push_back(
            it.second->Read(buffer_variables::name));
        it.second->status().SetWarningText(
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

  std::shared_ptr<std::set<std::shared_ptr<OpenBuffer>>> pending_buffers(
      new std::set<std::shared_ptr<OpenBuffer>>(),
      [this, exit_value,
       termination_type](std::set<std::shared_ptr<OpenBuffer>>* value) {
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
          if (it.second->dirty() &&
              !it.second->Read(buffer_variables::allow_dirty_delete)) {
            buffers_with_problems.push_back(
                it.second->Read(buffer_variables::name));
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

  auto decrement = [this, pending_buffers](
                       const std::shared_ptr<OpenBuffer>& buffer,
                       PossibleError) {
    pending_buffers->erase(buffer);
    std::wstring extra;
    std::wstring separator = L": ";
    int count = 0;
    for (auto& buffer : *pending_buffers) {
      if (count < 5) {
        extra += separator + buffer->name().read();
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
    it.second->PrepareToClose().SetConsumer(
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
    current_buffer()->mode()->ProcessInput(c);
    return futures::Past(EmptyValue());
  }

  return OpenAnonymousBuffer(*this).Transform(
      [this, c](std::shared_ptr<OpenBuffer> buffer) {
        if (!has_current_buffer()) {
          buffer_tree_.AddBuffer(buffer, BuffersList::AddBufferType::kOnlyList);
          set_current_buffer(buffer, CommandArgumentModeApplyMode::kFinal);
          CHECK(has_current_buffer());
          CHECK(current_buffer() == buffer);
        }
        buffer->mode()->ProcessInput(c);
        return EmptyValue();
      });
}

void EditorState::MoveBufferForwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer != nullptr) {
    it = buffers_.find(buffer->name());
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
  if (buffer != nullptr) {
    it = buffers_.find(buffer->name());
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
  if (buffer != nullptr) {
    PushPosition(buffer->position());
  }
}

void EditorState::PushPosition(LineColumn position) {
  auto buffer = current_buffer();
  if (buffer == nullptr ||
      !buffer->Read(buffer_variables::push_positions_to_history)) {
    return;
  }
  auto positions_buffer = futures::Past(std::shared_ptr<OpenBuffer>());
  if (auto buffer_it = buffers_.find(PositionsBufferName());
      buffer_it != buffers_.end()) {
    positions_buffer = futures::Past(buffer_it->second);
  } else {
    // Insert a new entry into the list of buffers.
    positions_buffer =
        OpenFile(OpenFileOptions{
                     .editor_state = *this,
                     .name = PositionsBufferName(),
                     .path = edge_path().empty()
                                 ? std::optional<Path>()
                                 : Path::Join(
                                       edge_path().front(),
                                       Path::FromString(L"positions").value()),
                     .insertion_type = BuffersList::AddBufferType::kIgnore})
            .Transform(
                [](std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator
                       buffer_it) {
                  CHECK(buffer_it->second != nullptr);
                  buffer_it->second->Set(buffer_variables::save_on_close, true);
                  buffer_it->second->Set(
                      buffer_variables::trigger_reload_on_buffer_write, false);
                  buffer_it->second->Set(buffer_variables::show_in_buffers_list,
                                         false);
                  return buffer_it->second;
                });
  }

  positions_buffer.SetConsumer(
      [line_to_insert = MakeNonNullShared<Line>(
           position.ToString() + L" " + buffer->Read(buffer_variables::name))](
          std::shared_ptr<OpenBuffer> buffer) {
        CHECK(buffer != nullptr);
        buffer->CheckPosition();
        CHECK_LE(buffer->position().line,
                 LineNumber(0) + buffer->contents().size());
        buffer->InsertLine(buffer->current_position_line(), line_to_insert);
        CHECK_LE(buffer->position().line,
                 LineNumber(0) + buffer->contents().size());
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

std::shared_ptr<OpenBuffer> EditorState::GetConsole() {
  auto it = buffers_.insert(make_pair(L"- console", nullptr));
  if (it.second) {  // Inserted the entry.
    CHECK(it.first->second == nullptr);
    it.first->second =
        OpenBuffer::New({.editor = *this, .name = it.first->first})
            .get_shared();
    it.first->second->Set(buffer_variables::allow_dirty_delete, true);
    it.first->second->Set(buffer_variables::show_in_buffers_list, false);
    it.first->second->Set(buffer_variables::persist_state, false);
  }
  return it.first->second;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(PositionsBufferName());
  return it != buffers_.end() &&
         it->second->contents().size() > LineNumberDelta(1);
}

BufferPosition EditorState::ReadPositionsStack() {
  CHECK(HasPositionsInStack());
  auto buffer = buffers_.find(PositionsBufferName())->second;
  return PositionFromLine(buffer->current_line()->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: Direction::kForwards
  // means the user is actually going "back" in the history, which means we have
  // to decrement the line counter.
  CHECK(HasPositionsInStack());
  auto buffer = buffers_.find(PositionsBufferName())->second;
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
  auto buffers = active_buffers();
  return futures::ForEachWithCopy(
             buffers.begin(), buffers.end(),
             [](const std::shared_ptr<OpenBuffer>& buffer) {
               return futures::Past(
                   buffer->Read(buffer_variables::pts)
                       ? futures::IterationControlCommand::kStop
                       : futures::IterationControlCommand::kContinue);
             })
      .Transform([](futures::IterationControlCommand c) {
        return c == futures::IterationControlCommand::kStop;
      })
      .Get()
      .value();
}

}  // namespace editor
}  // namespace afc
