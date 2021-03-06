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
#include "src/dirname.h"
#include "src/file_link_mode.h"
#include "src/open_file_command.h"
#include "src/run_command_handler.h"
#include "src/server.h"
#include "src/set_buffer_mode.h"
#include "src/set_variable_command.h"
#include "src/shapes.h"
#include "src/substring.h"
#include "src/terminal.h"
#include "src/time.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm_transformation.h"
#include "src/widget_list.h"
#include "src/wstring.h"

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

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using std::wstring;

template <typename MethodReturnType>
void RegisterBufferMethod(ObjectType* editor_type, const wstring& name,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  auto callback = std::make_unique<Value>(VMType::FUNCTION);
  // Returns nothing.
  callback->type.type_arguments = {VMType(VMType::VM_VOID),
                                   VMType::ObjectType(editor_type)};
  callback->callback = [method](vector<unique_ptr<Value>> args, Trampoline*) {
    CHECK_EQ(args.size(), size_t(1));
    CHECK_EQ(args[0]->type, VMType::ObjectType(L"Editor"));

    auto editor = static_cast<EditorState*>(args[0]->user_value.get());
    CHECK(editor != nullptr);
    return futures::Transform(
        editor->ForEachActiveBuffer(
            [method](const std::shared_ptr<OpenBuffer>& buffer) {
              CHECK(buffer != nullptr);
              (*buffer.*method)();
              return futures::Past(EmptyValue());
            }),
        [editor](EmptyValue) {
          editor->ResetModifiers();
          return EvaluationOutput::New(Value::NewVoid());
        });
  };
  editor_type->AddField(name, std::move(callback));
}
}  // namespace

void EditorState::NotifyInternalEvent() {
  VLOG(5) << "Internal event notification!";
  if (write(pipe_to_communicate_internal_events_.second, " ", 1) == -1) {
    status_.SetWarningText(L"Write to internal pipe failed: " +
                           FromByteString(strerror(errno)));
  }
}

// Executes pending work from all buffers.
void EditorState::ExecutePendingWork() { work_queue_.Execute(); }

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

WorkQueue* EditorState::work_queue() const { return &work_queue_; }

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

  auto editor_type = std::make_unique<ObjectType>(L"Editor");

  // Methods for Editor.
  RegisterVariableFields(editor_variables::BoolStruct(), editor_type.get(),
                         &EditorState::Read, &EditorState::Set);
  editor_type->AddField(
      L"AddVerticalSplit",
      vm::NewCallback([](EditorState* editor) { editor->AddVerticalSplit(); }));

  editor_type->AddField(
      L"EnterSetBufferMode", vm::NewCallback([](EditorState* editor) {
        editor->set_keyboard_redirect(NewSetBufferMode(editor));
      }));

  editor_type->AddField(L"AddHorizontalSplit",
                        vm::NewCallback([](EditorState* editor) {
                          editor->AddHorizontalSplit();
                        }));

  editor_type->AddField(L"SetHorizontalSplitsWithAllBuffers",
                        vm::NewCallback([](EditorState* editor) {
                          editor->SetHorizontalSplitsWithAllBuffers();
                        }));

  editor_type->AddField(L"SetActiveBuffer",
                        vm::NewCallback([](EditorState* editor, int delta) {
                          editor->SetActiveBuffer(delta);
                        }));

  editor_type->AddField(L"AdvanceActiveBuffer",
                        vm::NewCallback([](EditorState* editor, int delta) {
                          editor->AdvanceActiveBuffer(delta);
                        }));

  editor_type->AddField(L"AdvanceActiveLeaf",
                        vm::NewCallback([](EditorState* editor, int delta) {
                          editor->AdvanceActiveLeaf(delta);
                        }));

  editor_type->AddField(L"ZoomToLeaf", vm::NewCallback([](EditorState* editor) {
                          editor->ZoomToLeaf();
                        }));

  editor_type->AddField(
      L"SetVariablePrompt",
      vm::NewCallback([](EditorState* editor, std::wstring variable) {
        SetVariableCommandHandler(variable, editor);
      }));

  editor_type->AddField(L"home", vm::NewCallback([](EditorState* editor) {
                          return editor->home_directory().ToString();
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
          [](std::vector<std::unique_ptr<Value>> input,
             Trampoline* trampoline) {
            EditorState* editor =
                VMTypeMapper<EditorState*>::get(input[0].get());
            return futures::Transform(
                editor->ForEachActiveBuffer([callback =
                                                 std::move(input[1]->callback),
                                             trampoline](
                                                std::shared_ptr<OpenBuffer>
                                                    buffer) {
                  std::vector<std::unique_ptr<Value>> args;
                  args.push_back(
                      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
                          std::move(buffer)));
                  return futures::Transform(
                      callback(std::move(args), trampoline),
                      futures::Past(EmptyValue()));
                }),
                futures::Past(EvaluationOutput::Return(Value::NewVoid())));
          }));

  editor_type->AddField(
      L"ForEachActiveBufferWithRepetitions",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype})},
          [](std::vector<std::unique_ptr<Value>> input,
             Trampoline* trampoline) {
            EditorState* editor =
                VMTypeMapper<EditorState*>::get(input[0].get());
            return futures::Transform(
                editor->ForEachActiveBufferWithRepetitions(
                    [callback = std::move(input[1]->callback),
                     trampoline](std::shared_ptr<OpenBuffer> buffer) {
                      std::vector<std::unique_ptr<Value>> args;
                      args.push_back(
                          VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::
                              New(std::move(buffer)));
                      return futures::Transform(
                          callback(std::move(args), trampoline),
                          futures::Past(EmptyValue()));
                    }),
                futures::Past(EvaluationOutput::Return(Value::NewVoid())));
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
          [](std::vector<std::unique_ptr<Value>> args, Trampoline*) {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMTypeMapper<EditorState*>::vmtype);
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(editor != nullptr);
            auto target_path = Path::FromString(args[1]->str);
            if (target_path.IsError()) {
              editor->status()->SetWarningText(L"ConnectTo error: " +
                                               target_path.error().description);
              return futures::Past(
                  EvaluationOutput::Abort(target_path.error()));
            }
            OpenServerBuffer(editor, target_path.value());
            return futures::Past(EvaluationOutput::Return(Value::NewVoid()));
          }));

  editor_type->AddField(
      L"WaitForClose",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype,
           VMType::ObjectType(L"SetString")},
          [](vector<Value::Ptr> args, Trampoline*) {
            CHECK_EQ(args.size(), 2u);
            auto editor = static_cast<EditorState*>(args[0]->user_value.get());
            const auto& buffers_to_wait =
                *static_cast<std::set<wstring>*>(args[1]->user_value.get());

            auto values =
                std::make_shared<std::vector<futures::Value<EmptyValue>>>();
            for (const auto& buffer_name : buffers_to_wait) {
              auto buffer_it = editor->buffers()->find(buffer_name);
              if (buffer_it == editor->buffers()->end()) {
                continue;
              }
              futures::Future<EmptyValue> future;
              buffer_it->second->AddCloseObserver(
                  [consumer = std::move(future.consumer)]() {
                    LOG(INFO) << "Buffer is closing";
                    consumer(EmptyValue());
                  });
              values->push_back(std::move(future.value));
            }
            return futures::Transform(
                futures::ForEach(
                    values->begin(), values->end(),
                    [values](futures::Value<EmptyValue> future) {
                      return futures::Transform(
                          future,
                          futures::Past(
                              futures::IterationControlCommand::kContinue));
                    }),
                futures::Past(EvaluationOutput::Return(Value::NewVoid())));
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
                          NewOpenFileCommand(editor)->ProcessInput(0, editor);
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
        return ForkCommand(editor, *options);
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
          [](std::vector<std::unique_ptr<Value>> args, Trampoline*) {
            CHECK_EQ(args.size(), 3u);
            CHECK_EQ(args[0]->type, VMTypeMapper<EditorState*>::vmtype);
            CHECK(args[1]->IsString());
            CHECK(args[2]->IsBool());
            OpenFileOptions options;
            options.editor_state =
                static_cast<EditorState*>(args[0]->user_value.get());
            CHECK(options.editor_state != nullptr);
            if (auto path = Path::FromString(args[1]->str); !path.IsError()) {
              options.path = std::move(path.value());
            }
            options.insertion_type = args[2]->boolean
                                         ? BuffersList::AddBufferType::kVisit
                                         : BuffersList::AddBufferType::kIgnore;
            return futures::Transform(
                OpenFile(options),
                [](map<wstring, shared_ptr<OpenBuffer>>::iterator result) {
                  return EvaluationOutput::Return(
                      Value::NewObject(L"Buffer", result->second));
                });
          }));

  editor_type->AddField(
      L"AddBinding",
      Value::NewFunction(
          {VMType::Void(), VMTypeMapper<EditorState*>::vmtype, VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [](vector<unique_ptr<Value>> args) {
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

  OpenBuffer::RegisterBufferType(this, environment.get());

  InitShapes(environment.get());
  RegisterTransformations(this, environment.get());
  Modifiers::Register(environment.get());
  ForkCommandOptions::Register(environment.get());
  LineColumn::Register(environment.get());
  Range::Register(environment.get());
  return environment;
}

std::pair<int, int> BuildPipe() {
  int output[2];
  if (pipe2(output, O_NONBLOCK) == -1) {
    return {-1, -1};
  }
  return {output[0], output[1]};
}

EditorState::EditorState(CommandLineValues args, AudioPlayer* audio_player)
    : bool_variables_(editor_variables::BoolStruct()->NewInstance()),
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
      default_commands_(NewCommandMode(this)),
      pipe_to_communicate_internal_events_(BuildPipe()),
      audio_player_(audio_player),
      buffer_tree_(this, std::make_unique<WidgetListHorizontal>(
                             this, BufferWidget::New())),
      status_(GetConsole(), audio_player_),
      work_queue_([this] { NotifyInternalEvent(); }) {
  auto paths = edge_path();
  futures::ForEach(paths.begin(), paths.end(), [this](Path dir) {
    auto path = Path::Join(dir, Path::FromString(L"hooks/start.cc").value());
    wstring error_description;
    std::shared_ptr<Expression> expression = CompileFile(
        ToByteString(path.ToString()), environment_, &error_description);
    if (expression == nullptr) {
      LOG(INFO) << "Compilation error for " << path << ": "
                << error_description;
      status_.SetWarningText(path.ToString() + L": error: " +
                             error_description);
      return futures::Past(futures::IterationControlCommand::kContinue);
    }
    LOG(INFO) << "Evaluating file: " << path;
    return futures::Transform(
        Evaluate(
            expression.get(), environment_,
            [path, work_queue = work_queue()](std::function<void()> resume) {
              LOG(INFO) << "Evaluation of file yields: " << path;
              work_queue->Schedule(std::move(resume));
            }),
        futures::Past(futures::IterationControlCommand::kContinue));
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
  if (variable == editor_variables::focus) {
    AdjustWidgets();
  }
}

void EditorState::toggle_bool_variable(const EdgeVariable<bool>* variable) {
  Set(variable, modifiers().repetitions.has_value()
                    ? modifiers().repetitions != 0
                    : !Read(variable));
}

void EditorState::CheckPosition() {
  auto buffer = buffer_tree_.GetActiveLeaf()->Lock();
  if (buffer != nullptr) {
    buffer->CheckPosition();
  }
}

void EditorState::CloseBuffer(OpenBuffer* buffer) {
  CHECK(buffer != nullptr);
  buffer->PrepareToClose().SetConsumer([this, buffer](PossibleError error) {
    if (error.IsError()) {
      buffer->status()->SetWarningText(
          L"🖝  Unable to close (“*ad” to ignore): " +
          error.error().description + L": " +
          buffer->Read(buffer_variables::name));
      return;
    }

    buffer->Close();
    auto index = buffer_tree_.GetBufferIndex(buffer);
    buffer_tree_.RemoveBuffer(buffer);
    buffers_.erase(buffer->Read(buffer_variables::name));
    AdjustWidgets();
    LOG(INFO) << "Adjusting widgets that may be displaying the buffer we "
                 "are deleting.";
    if (buffer_tree_.BuffersCount() == 0) return;
    auto replacement =
        buffer_tree_.GetBuffer(index.value_or(0) % buffer_tree_.BuffersCount());
    buffer_tree_.ForEachBufferWidget([&](BufferWidget* widget) {
      auto widget_buffer = widget->Lock();
      if (widget_buffer == nullptr || widget_buffer.get() == buffer) {
        widget->SetBuffer(replacement);
      }
    });
  });
}

void EditorState::set_current_buffer(std::shared_ptr<OpenBuffer> buffer,
                                     CommandArgumentModeApplyMode apply_mode) {
  buffer_tree_.GetActiveLeaf()->SetBuffer(buffer);
  if (!Read(editor_variables::focus)) {
    AdjustWidgets();
  }
  if (buffer != nullptr) {
    if (apply_mode == CommandArgumentModeApplyMode::kFinal) {
      buffer->Visit();
    } else {
      buffer->Enter();
    }
  }
}

void EditorState::AddVerticalSplit() {
  auto casted_child = dynamic_cast<WidgetListVertical*>(buffer_tree_.Child());
  if (casted_child == nullptr) {
    buffer_tree_.WrapChild([this](std::unique_ptr<Widget> child) {
      return std::make_unique<WidgetListVertical>(this, std::move(child));
    });
    casted_child = dynamic_cast<WidgetListVertical*>(buffer_tree_.Child());
    CHECK(casted_child != nullptr);
  }
  OpenAnonymousBuffer(this).SetConsumer(
      [casted_child](std::shared_ptr<OpenBuffer> buffer) {
        casted_child->AddChild(BufferWidget::New(buffer));
      });
}

void EditorState::AddHorizontalSplit() {
  auto casted_child = dynamic_cast<WidgetListHorizontal*>(buffer_tree_.Child());
  if (casted_child == nullptr) {
    buffer_tree_.WrapChild([this](std::unique_ptr<Widget> child) {
      return std::make_unique<WidgetListHorizontal>(this, std::move(child));
    });
    casted_child = dynamic_cast<WidgetListHorizontal*>(buffer_tree_.Child());
    CHECK(casted_child != nullptr);
  }
  OpenAnonymousBuffer(this).SetConsumer(
      [casted_child](std::shared_ptr<OpenBuffer> buffer) {
        casted_child->AddChild(BufferWidget::New(buffer));
      });
}

void EditorState::SetHorizontalSplitsWithAllBuffers() {
  auto active_buffer = current_buffer();
  std::vector<std::unique_ptr<Widget>> buffers;
  buffers.reserve(buffer_tree_.BuffersCount());
  size_t index_active = 0;
  for (size_t index = 0; index < buffer_tree_.BuffersCount(); index++) {
    auto buffer = buffer_tree_.GetBuffer(index);
    if (buffer == nullptr ||
        !buffer->Read(buffer_variables::show_in_buffers_list)) {
      continue;
    }
    if (buffer == active_buffer) {
      index_active = buffers.size();
    }
    buffers.push_back(BufferWidget::New(buffer));
  }
  if (buffer_tree_.BuffersCount() == 0) {
    return;
  }
  buffer_tree_.SetChild(std::make_unique<WidgetListHorizontal>(
      this, std::move(buffers), index_active));
}

void EditorState::SetActiveBuffer(size_t position) {
  set_current_buffer(
      buffer_tree_.GetBuffer(position % buffer_tree_.BuffersCount()),
      CommandArgumentModeApplyMode::kFinal);
}

void EditorState::AdvanceActiveLeaf(int delta) {
  size_t leaves = buffer_tree_.CountLeaves();
  LOG(INFO) << "AdvanceActiveLeaf with delta " << delta << " and leaves "
            << leaves;
  if (delta < 0) {
    delta = leaves - ((-delta) % leaves);
  } else {
    delta %= leaves;
  }
  VLOG(5) << "Delta adjusted to: " << delta;
  delta = buffer_tree_.AdvanceActiveLeafWithoutWrapping(delta);
  VLOG(6) << "After first advance, delta remaining: " << delta;
  if (delta > 0) {
    VLOG(7) << "Wrapping around end of tree";
    buffer_tree_.SetActiveLeavesAtStart();
    delta--;
  }
  delta = buffer_tree_.AdvanceActiveLeafWithoutWrapping(delta);
  VLOG(5) << "Done advance, with delta: " << delta;
}

void EditorState::AdvanceActiveBuffer(int delta) {
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

void EditorState::ZoomToLeaf() {
  buffer_tree_.SetChild(
      BufferWidget::New(buffer_tree_.GetActiveLeaf()->Lock()));
}

void EditorState::AdjustWidgets() {
  if (Read(editor_variables::focus)) {
    ZoomToLeaf();
    return;
  }
  SetHorizontalSplitsWithAllBuffers();
}

bool EditorState::has_current_buffer() const {
  return current_buffer() != nullptr;
}
shared_ptr<OpenBuffer> EditorState::current_buffer() {
  auto leaf = buffer_tree_.GetActiveLeaf();
  CHECK(leaf != nullptr);
  return leaf->Lock();
}
const shared_ptr<OpenBuffer> EditorState::current_buffer() const {
  return buffer_tree_.GetActiveLeaf()->Lock();
}

std::vector<std::shared_ptr<OpenBuffer>> EditorState::active_buffers() const {
  std::vector<std::shared_ptr<OpenBuffer>> output;
  if (status()->GetType() == Status::Type::kPrompt) {
    output.push_back(status()->prompt_buffer());
  } else if (Read(editor_variables::multiple_buffers)) {
    std::set<const Widget*> widgets;
    std::set<const Widget*> widgets_to_expand;
    widgets_to_expand.insert(&buffer_tree_);
    while (!widgets_to_expand.empty()) {
      const Widget* widget = *widgets_to_expand.begin();
      widgets_to_expand.erase(widgets_to_expand.begin());
      if (!widgets.insert(widget).second) continue;
      widget->ForEachBufferWidgetConst([&](const BufferWidget* widget) {
        widgets_to_expand.insert(widget);
      });
    }

    std::unordered_set<const BufferWidget*> leafs;
    for (auto* widget : widgets) {
      auto leaf = widget->GetActiveLeaf();
      if (leaf != nullptr) {
        leafs.insert(leaf);
      }
    }

    std::set<OpenBuffer*> buffers_seen;
    for (auto* leaf : leafs) {
      auto buffer = leaf->Lock();
      if (buffer != nullptr && buffers_seen.insert(buffer.get()).second) {
        output.push_back(buffer);
      }
    }
  } else if (auto buffer = current_buffer(); buffer != nullptr) {
    if (buffer->status()->GetType() == Status::Type::kPrompt) {
      buffer = buffer->status()->prompt_buffer();
    }
    output.push_back(buffer);
  }
  return output;
}

void EditorState::AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                            BuffersList::AddBufferType insertion_type) {
  auto initial_active_buffers = active_buffers();
  buffer_tree()->AddBuffer(buffer, insertion_type);
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
    std::function<
        futures::Value<EmptyValue>(const std::shared_ptr<OpenBuffer>&)>
        callback) {
  auto buffers = active_buffers();
  return futures::Transform(
      futures::ForEachWithCopy(
          buffers.begin(), buffers.end(),
          [callback](const std::shared_ptr<OpenBuffer>& buffer) {
            return futures::Transform(
                callback(buffer),
                futures::Past(futures::IterationControlCommand::kContinue));
          }),
      futures::Past(EmptyValue()));
}

futures::Value<EmptyValue> EditorState::ForEachActiveBufferWithRepetitions(
    std::function<
        futures::Value<EmptyValue>(const std::shared_ptr<OpenBuffer>&)>
        callback) {
  auto value = futures::Past(EmptyValue());
  if (!modifiers().repetitions.has_value()) {
    value = ForEachActiveBuffer(callback);
  } else if (auto buffer = buffer_tree()->GetBuffer(
                 (max(modifiers().repetitions.value(), 1ul) - 1) %
                 buffer_tree()->BuffersCount());
             buffer != nullptr) {
    value = callback(buffer);
  }
  return futures::Transform(value, [this](EmptyValue) {
    ResetModifiers();
    return EmptyValue();
  });
}

futures::Value<EmptyValue> EditorState::ApplyToActiveBuffers(
    transformation::Variant transformation) {
  return ForEachActiveBuffer([transformation = std::move(transformation)](
                                 const std::shared_ptr<OpenBuffer>& buffer) {
    return buffer->ApplyToCursors(transformation);
  });
}

wstring GetBufferName(const wstring& prefix, size_t count) {
  return prefix + L" " + std::to_wstring(count);
}

wstring EditorState::GetUnusedBufferName(const wstring& prefix) {
  size_t count = 0;
  while (buffers()->find(GetBufferName(prefix, count)) != buffers()->end()) {
    count++;
  }
  return GetBufferName(prefix, count);
}

void EditorState::Terminate(TerminationType termination_type, int exit_value) {
  if (termination_type == TerminationType::kWhenClean) {
    LOG(INFO) << "Checking buffers for termination.";
    std::vector<wstring> buffers_with_problems;
    for (auto& it : buffers_) {
      if (auto result = it.second->IsUnableToPrepareToClose();
          result.IsError()) {
        buffers_with_problems.push_back(
            it.second->Read(buffer_variables::name));
        it.second->status()->SetWarningText(
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

  std::shared_ptr<int> pending_calls(
      new int(buffers_.size()),
      [this, exit_value, termination_type](int* value) {
        if (*value != 0) {
          LOG(INFO) << "Termination attempt didn't complete successfully. It "
                       "must mean that a new one has started.";
          return;
        }
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
        exit_value_ = exit_value;
      });

  for (auto& it : buffers_) {
    it.second->PrepareToClose().SetConsumer(
        [pending_calls](PossibleError) { --*pending_calls; });
  }
}

void EditorState::ProcessInput(int c) {
  if (auto handler = keyboard_redirect().get(); handler != nullptr) {
    handler->ProcessInput(c, this);
    return;
  }

  if (has_current_buffer()) {
    current_buffer()->mode()->ProcessInput(c, this);
    return;
  }

  OpenAnonymousBuffer(this).SetConsumer(
      [this, c](std::shared_ptr<OpenBuffer> buffer) {
        if (!has_current_buffer()) {
          set_current_buffer(buffer, CommandArgumentModeApplyMode::kFinal);
        }
        buffer->mode()->ProcessInput(c, this);
        CHECK(has_current_buffer());
      });
}

void EditorState::MoveBufferForwards(size_t times) {
  auto it = buffers_.end();

  auto buffer = current_buffer();
  if (buffer != nullptr) {
    it = buffers_.find(buffer->Read(buffer_variables::name));
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
    it = buffers_.find(buffer->Read(buffer_variables::name));
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
    work_queue_.ScheduleAt(next_screen_update_, [] {});
    return {};
  }
  std::unique_lock<std::mutex> lock(mutex_);
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

static wstring kPositionsBufferName = L"- positions";

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
  if (auto buffer_it = buffers_.find(kPositionsBufferName);
      buffer_it != buffers_.end()) {
    positions_buffer = futures::Past(buffer_it->second);
  } else {
    // Insert a new entry into the list of buffers.
    OpenFileOptions options;
    options.editor_state = this;
    options.name = kPositionsBufferName;
    if (!edge_path().empty()) {
      options.path = Path::Join(edge_path().front(),
                                Path::FromString(L"positions").value());
    }
    options.insertion_type = BuffersList::AddBufferType::kIgnore;
    positions_buffer = futures::Transform(
        OpenFile(options),
        [](map<wstring, shared_ptr<OpenBuffer>>::iterator buffer_it) {
          CHECK(buffer_it->second != nullptr);
          buffer_it->second->Set(buffer_variables::save_on_close, true);
          buffer_it->second->Set(
              buffer_variables::trigger_reload_on_buffer_write, false);
          buffer_it->second->Set(buffer_variables::show_in_buffers_list, false);
          return buffer_it->second;
        });
  }

  positions_buffer.SetConsumer(
      [line_to_insert = std::make_shared<Line>(
           position.ToString() + L" " + buffer->Read(buffer_variables::name))](
          std::shared_ptr<OpenBuffer> buffer) {
        CHECK(buffer != nullptr);
        buffer->CheckPosition();
        CHECK_LE(buffer->position().line,
                 LineNumber(0) + buffer->contents()->size());
        buffer->InsertLine(buffer->current_position_line(), line_to_insert);
        CHECK_LE(buffer->position().line,
                 LineNumber(0) + buffer->contents()->size());
      });
}

static BufferPosition PositionFromLine(const wstring& line) {
  std::wstringstream line_stream(line);
  BufferPosition pos;
  line_stream >> pos.position.line.line >> pos.position.column.column;
  line_stream.get();
  getline(line_stream, pos.buffer_name);
  return pos;
}

std::shared_ptr<OpenBuffer> EditorState::GetConsole() {
  auto it = buffers_.insert(make_pair(L"- console", nullptr));
  if (it.second) {  // Inserted the entry.
    CHECK(it.first->second == nullptr);
    it.first->second =
        OpenBuffer::New({.editor = this, .name = it.first->first});
    it.first->second->Set(buffer_variables::allow_dirty_delete, true);
    it.first->second->Set(buffer_variables::show_in_buffers_list, false);
    it.first->second->Set(buffer_variables::persist_state, false);
  }
  return it.first->second;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() &&
         it->second->contents()->size() > LineNumberDelta(1);
}

BufferPosition EditorState::ReadPositionsStack() {
  CHECK(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  return PositionFromLine(buffer->current_line()->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: Direction::kForwards
  // means the user is actually going "back" in the history, which means we have
  // to decrement the line counter.
  CHECK(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
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

Status* EditorState::status() { return &status_; }
const Status* EditorState::status() const { return &status_; }

wstring EditorState::expand_path(const wstring& path_str) const {
  if (auto path = Path::FromString(path_str); !path.IsError()) {
    return Path::ExpandHomeDirectory(home_directory(), path.value()).ToString();
  }
  return path_str;
}

void EditorState::ProcessSignals() {
  if (pending_signals_.empty()) {
    return;
  }
  vector<int> signals;
  signals.swap(pending_signals_);
  for (int signal : signals) {
    switch (signal) {
      case SIGINT:
      case SIGTSTP:
        ForEachActiveBuffer(
            [signal](const std::shared_ptr<OpenBuffer>& buffer) {
              buffer->PushSignal(signal);
              return futures::Past(EmptyValue());
            });
    }
  }
}

bool EditorState::handling_stop_signals() const {
  auto buffers = active_buffers();
  return futures::Transform(
             futures::ForEachWithCopy(
                 buffers.begin(), buffers.end(),
                 [](const std::shared_ptr<OpenBuffer>& buffer) {
                   return futures::Past(
                       buffer->Read(buffer_variables::pts)
                           ? futures::IterationControlCommand::kStop
                           : futures::IterationControlCommand::kContinue);
                 }),
             [](futures::IterationControlCommand c) {
               return c == futures::IterationControlCommand::kStop;
             })
      .Get()
      .value();
}

}  // namespace editor
}  // namespace afc
