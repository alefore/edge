#include "src/editor_vm.h"

#include "src/buffer_vm.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/value_or_error.h"
#include "src/language/wstring.h"
#include "src/line_column_vm.h"
#include "src/open_file_command.h"
#include "src/run_command_handler.h"
#include "src/server.h"
#include "src/set_buffer_mode.h"
#include "src/set_variable_command.h"
#include "src/shapes.h"
#include "src/terminal.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/set.h"
#include "src/vm_transformation.h"

using afc::language::Pointer;

namespace afc::vm {
template <>
struct VMTypeMapper<editor::EditorState> {
  static editor::EditorState& get(Value& value) {
    return Pointer(static_cast<editor::EditorState*>(
                       value.get_user_value(vmtype).get()))
        .Reference();
  }

  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::EditorState>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Editor"));
}  // namespace afc::vm

namespace afc::editor {
using infrastructure::Path;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

namespace gc = language::gc;
namespace {
template <typename MethodReturnType>
void RegisterBufferMethod(gc::Pool& pool, ObjectType& editor_type,
                          const wstring& name,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  editor_type.AddField(
      name,
      Value::NewFunction(
          pool,
          // Returns nothing.
          {VMType::Void(), editor_type.type()},
          [method](std::vector<gc::Root<Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0].ptr()->type,
                     VMTypeMapper<editor::EditorState>::vmtype);

            EditorState& editor =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            return editor
                .ForEachActiveBuffer([method](OpenBuffer& buffer) {
                  (buffer.*method)();
                  return futures::Past(EmptyValue());
                })
                .Transform([&editor, &pool = trampoline.pool()](EmptyValue) {
                  editor.ResetModifiers();
                  return EvaluationOutput::New(Value::NewVoid(pool));
                });
          }));
}

template <typename EdgeStruct, typename FieldValue>
void RegisterVariableFields(
    gc::Pool& pool, EdgeStruct* edge_struct, afc::vm::ObjectType& editor_type,
    const FieldValue& (EditorState::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (EditorState::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    editor_type.AddField(
        variable->name(),
        vm::NewCallback(pool, [reader, variable](EditorState& editor) {
          return (editor.*reader)(variable);
        }));

    // Setter.
    editor_type.AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            pool, [variable, setter](EditorState& editor, FieldValue value) {
              (editor.*setter)(variable, value);
            }));
  }
}
}  // namespace

gc::Root<Environment> BuildEditorEnvironment(EditorState& editor) {
  gc::Pool& pool = editor.gc_pool();
  gc::Root<Environment> environment =
      pool.NewRoot(MakeNonNullUnique<Environment>(
          afc::vm::Environment::NewDefault(pool).ptr()));
  Environment& value = environment.ptr().value();
  value.Define(L"terminal_backspace",
               Value::NewString(pool, {Terminal::BACKSPACE}));
  value.Define(L"terminal_control_a",
               Value::NewString(pool, {Terminal::CTRL_A}));
  value.Define(L"terminal_control_e",
               Value::NewString(pool, {Terminal::CTRL_E}));
  value.Define(L"terminal_control_d",
               Value::NewString(pool, {Terminal::CTRL_D}));
  value.Define(L"terminal_control_k",
               Value::NewString(pool, {Terminal::CTRL_K}));
  value.Define(L"terminal_control_u",
               Value::NewString(pool, {Terminal::CTRL_U}));

  auto editor_type =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<editor::EditorState>::vmtype);

  // Methods for Editor.
  RegisterVariableFields<EdgeStruct<bool>, bool>(
      pool, editor_variables::BoolStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<wstring>, wstring>(
      pool, editor_variables::StringStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<int>, int>(
      pool, editor_variables::IntStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  editor_type->AddField(
      L"EnterSetBufferMode", vm::NewCallback(pool, [](EditorState& editor) {
        editor.set_keyboard_redirect(NewSetBufferMode(editor));
      }));

  editor_type->AddField(
      L"SetActiveBuffer",
      vm::NewCallback(pool, [](EditorState& editor, int delta) {
        editor.SetActiveBuffer(delta);
      }));

  editor_type->AddField(
      L"AdvanceActiveBuffer",
      vm::NewCallback(pool, [](EditorState& editor, int delta) {
        editor.AdvanceActiveBuffer(delta);
      }));

  editor_type->AddField(L"ZoomToLeaf",
                        vm::NewCallback(pool, [](EditorState&) {}));

  editor_type->AddField(
      L"SetVariablePrompt",
      vm::NewCallback(pool, [](EditorState& editor, std::wstring variable) {
        SetVariableCommandHandler(variable, editor);
      }));

  editor_type->AddField(L"home", vm::NewCallback(pool, [](EditorState& editor) {
                          return editor.home_directory().read();
                        }));

  editor_type->AddField(
      L"pop_repetitions", vm::NewCallback(pool, [](EditorState& editor) {
        auto value = static_cast<int>(editor.repetitions().value_or(1));
        editor.ResetRepetitions();
        return value;
      }));

  editor_type->AddField(
      L"ForEachActiveBuffer",
      Value::NewFunction(
          pool,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype})},
          [&pool = pool](std::vector<gc::Root<Value>> input,
                         Trampoline& trampoline) {
            EditorState& editor =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            NonNull<std::shared_ptr<PossibleError>> output;
            return editor
                .ForEachActiveBuffer([callback = input[1].ptr()->LockCallback(),
                                      &trampoline, output](OpenBuffer& buffer) {
                  std::vector<gc::Root<Value>> args;
                  args.push_back(
                      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
                          trampoline.pool(), buffer.shared_from_this()));
                  return callback(std::move(args), trampoline)
                      .Transform([](EvaluationOutput) { return Success(); })
                      .ConsumeErrors([output](Error error) {
                        output.value() = error;
                        return futures::Past(EmptyValue());
                      });
                })
                .Transform([output, &pool](
                               EmptyValue) -> ValueOrError<EvaluationOutput> {
                  if (output->IsError()) return output->error();
                  return EvaluationOutput::Return(Value::NewVoid(pool));
                });
          }));

  editor_type->AddField(
      L"ForEachActiveBufferWithRepetitions",
      Value::NewFunction(
          pool,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::vmtype})},
          [&pool = pool](std::vector<gc::Root<Value>> input,
                         Trampoline& trampoline) {
            EditorState& editor =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            return editor
                .ForEachActiveBufferWithRepetitions([callback =
                                                         input[1]
                                                             .ptr()
                                                             ->LockCallback(),
                                                     &trampoline](
                                                        OpenBuffer& buffer) {
                  std::vector<gc::Root<Value>> args;
                  args.push_back(
                      VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>::New(
                          trampoline.pool(), buffer.shared_from_this()));
                  return callback(std::move(args), trampoline)
                      .Transform([](EvaluationOutput) { return Success(); })
                      // TODO(easy): Don't ConsumeErrors; change
                      // ForEachActiveBuffer.
                      .ConsumeErrors(
                          [](Error) { return futures::Past(EmptyValue()); });
                })
                .Transform([&pool](EmptyValue) {
                  return EvaluationOutput::Return(Value::NewVoid(pool));
                });
          }));

  editor_type->AddField(L"ProcessInput",
                        vm::NewCallback(pool, [](EditorState& editor, int c) {
                          editor.ProcessInput(c);
                        }));

  editor_type->AddField(
      L"ConnectTo",
      Value::NewFunction(
          pool,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype, VMType::String()},
          [&pool = pool](std::vector<gc::Root<Value>> args, Trampoline&)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2u);
            EditorState& editor =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            auto target_path = Path::FromString(args[1].ptr()->get_string());
            if (target_path.IsError()) {
              editor.status().SetWarningText(L"ConnectTo error: " +
                                             target_path.error().description);
              return futures::Past(target_path.error());
            }
            OpenServerBuffer(editor, target_path.value());
            return futures::Past(
                EvaluationOutput::Return(Value::NewVoid(pool)));
          }));

  editor_type->AddField(
      L"WaitForClose",
      Value::NewFunction(
          pool,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMTypeMapper<std::set<std::wstring>*>::vmtype},
          [&pool = pool](std::vector<gc::Root<Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 2u);
            EditorState& editor =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            const auto& buffers_to_wait =
                *VMTypeMapper<std::set<std::wstring>*>::get(
                    args[1].ptr().value());

            auto values =
                std::make_shared<std::vector<futures::Value<EmptyValue>>>();
            for (const auto& buffer_name_str : buffers_to_wait) {
              if (auto buffer_it =
                      editor.buffers()->find(BufferName(buffer_name_str));
                  buffer_it != editor.buffers()->end()) {
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
                .Transform([&pool](futures::IterationControlCommand) {
                  return EvaluationOutput::Return(Value::NewVoid(pool));
                });
          }));

  editor_type->AddField(L"SendExitTo",
                        vm::NewCallback(pool, [](EditorState&, wstring args) {
                          int fd = open(ToByteString(args).c_str(), O_WRONLY);
                          string command = "editor.Exit(0);\n";
                          write(fd, command.c_str(), command.size());
                          close(fd);
                        }));

  editor_type->AddField(L"Exit",
                        vm::NewCallback(pool, [](EditorState&, int status) {
                          LOG(INFO) << "Exit: " << status;
                          exit(status);
                        }));

  editor_type->AddField(
      L"SetStatus", vm::NewCallback(pool, [](EditorState& editor, wstring s) {
        editor.status().SetInformationText(s);
      }));

  editor_type->AddField(L"PromptAndOpenFile",
                        vm::NewCallback(pool, [](EditorState& editor) {
                          NewOpenFileCommand(editor)->ProcessInput(0);
                        }));

  editor_type->AddField(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(pool, [](EditorState& editor, bool value) {
        editor.set_screen_needs_hard_redraw(value);
      }));

  editor_type->AddField(
      L"set_exit_value",
      vm::NewCallback(pool, [](EditorState& editor, int exit_value) {
        editor.set_exit_value(exit_value);
      }));

  editor_type->AddField(
      L"ForkCommand", vm::NewCallback(pool, [](EditorState& editor,
                                               ForkCommandOptions* options) {
        return std::move(ForkCommand(editor, *options).get_shared());
      }));

  editor_type->AddField(
      L"repetitions", vm::NewCallback(pool, [](EditorState& editor) {
        // TODO: Somehow expose the optional to the VM.
        return static_cast<int>(editor.repetitions().value_or(1));
      }));

  editor_type->AddField(
      L"set_repetitions",
      vm::NewCallback(pool, [](EditorState& editor, int times) {
        editor.set_repetitions(times);
      }));

  editor_type->AddField(
      L"OpenFile",
      Value::NewFunction(
          pool,
          {VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype,
           VMTypeMapper<EditorState>::vmtype, VMType::String(), VMType::Bool()},
          [&pool = pool](std::vector<gc::Root<Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 3u);
            EditorState& editor_state =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            return OpenOrCreateFile(
                       OpenFileOptions{
                           .editor_state = editor_state,
                           .path = Path::FromString(args[1].ptr()->get_string())
                                       .AsOptional(),
                           .insertion_type =
                               args[2].ptr()->get_bool()
                                   ? BuffersList::AddBufferType::kVisit
                                   : BuffersList::AddBufferType::kIgnore})
                .Transform(
                    [&pool](NonNull<std::shared_ptr<OpenBuffer>> buffer) {
                      return EvaluationOutput::Return(Value::NewObject(
                          pool,
                          VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype
                              .object_type,
                          buffer.get_shared()));
                    });
          }));

  editor_type->AddField(
      L"AddBinding",
      Value::NewFunction(
          pool,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype, VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [&pool = pool](std::vector<gc::Root<Value>> args) {
            CHECK_EQ(args.size(), 4u);
            EditorState& editor =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            editor.default_commands()->Add(
                args[1].ptr()->get_string(), args[2].ptr()->get_string(),
                std::move(args[3]), editor.environment());
            return Value::NewVoid(pool);
          }));

  RegisterBufferMethod(pool, editor_type.value(), L"ToggleActiveCursors",
                       &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"PushActiveCursors",
                       &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"PopActiveCursors",
                       &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"SetActiveCursorsToMarks",
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(pool, editor_type.value(), L"CreateCursor",
                       &OpenBuffer::CreateCursor);
  RegisterBufferMethod(pool, editor_type.value(), L"DestroyCursor",
                       &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(pool, editor_type.value(), L"DestroyOtherCursors",
                       &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"RepeatLastTransformation",
                       &OpenBuffer::RepeatLastTransformation);

  value.Define(L"editor",
               Value::NewObject(pool, editor_type->type().object_type,
                                shared_ptr<void>(&editor, [](void*) {})));

  value.DefineType(std::move(editor_type));

  value.DefineType(BuildBufferType(pool));

  InitShapes(pool, value);
  RegisterTransformations(pool, value);
  Modifiers::Register(pool, value);
  ForkCommandOptions::Register(pool, value);
  LineColumnRegister(pool, value);
  RangeRegister(pool, value);
  return environment;
}
}  // namespace afc::editor
