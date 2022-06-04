#include "src/editor_vm.h"

#include "src/buffer_vm.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/insert_history_buffer.h"
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
#include "src/transformation/vm.h"
#include "src/vm/public/callbacks.h"

using afc::language::NonNull;
using afc::language::Pointer;

namespace afc::vm {
template <>
struct VMTypeMapper<editor::EditorState> {
  static editor::EditorState& get(Value& value) {
    return value.get_user_value<editor::EditorState>(vmtype).value();
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
using language::overload;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using vm::Environment;
using vm::EvaluationOutput;
using vm::ObjectType;
using vm::PurityType;
using vm::Trampoline;
using vm::VMType;
using vm::VMTypeMapper;

namespace gc = language::gc;
namespace {
template <typename MethodReturnType>
void RegisterBufferMethod(gc::Pool& pool, ObjectType& editor_type,
                          const std::wstring& name, PurityType purity_type,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  editor_type.AddField(
      name,
      vm::Value::NewFunction(
          pool, purity_type, {VMType::Void(), editor_type.type()},
          [method](std::vector<gc::Root<vm::Value>> args,
                   Trampoline& trampoline) {
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
                  return EvaluationOutput::New(vm::Value::NewVoid(pool));
                });
          }));
}

template <typename EdgeStruct, typename FieldValue>
void RegisterVariableFields(
    gc::Pool& pool, EdgeStruct* edge_struct, afc::vm::ObjectType& editor_type,
    const FieldValue& (EditorState::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (EditorState::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  std::vector<std::wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const std::wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    editor_type.AddField(
        variable->name(),
        vm::NewCallback(pool, PurityType::kReader,
                        [reader, variable](EditorState& editor) {
                          return (editor.*reader)(variable);
                        }));

    // Setter.
    editor_type.AddField(L"set_" + variable->name(),
                         vm::NewCallback(pool, PurityType::kUnknown,
                                         [variable, setter](EditorState& editor,
                                                            FieldValue value) {
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
               vm::Value::NewString(pool, {Terminal::BACKSPACE}));
  value.Define(L"terminal_control_a",
               vm::Value::NewString(pool, {Terminal::CTRL_A}));
  value.Define(L"terminal_control_e",
               vm::Value::NewString(pool, {Terminal::CTRL_E}));
  value.Define(L"terminal_control_d",
               vm::Value::NewString(pool, {Terminal::CTRL_D}));
  value.Define(L"terminal_control_k",
               vm::Value::NewString(pool, {Terminal::CTRL_K}));
  value.Define(L"terminal_control_u",
               vm::Value::NewString(pool, {Terminal::CTRL_U}));

  auto editor_type =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<editor::EditorState>::vmtype);

  // Methods for Editor.
  RegisterVariableFields<EdgeStruct<bool>, bool>(
      pool, editor_variables::BoolStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<std::wstring>, std::wstring>(
      pool, editor_variables::StringStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<int>, int>(
      pool, editor_variables::IntStruct(), editor_type.value(),
      &EditorState::Read, &EditorState::Set);

  editor_type->AddField(
      L"EnterSetBufferMode",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        editor_arg.set_keyboard_redirect(NewSetBufferMode(editor_arg));
      }));

  editor_type->AddField(L"SetActiveBuffer",
                        vm::NewCallback(pool, PurityType::kUnknown,
                                        [](EditorState& editor_arg, int delta) {
                                          editor_arg.SetActiveBuffer(delta);
                                        }));

  editor_type->AddField(L"AdvanceActiveBuffer",
                        vm::NewCallback(pool, PurityType::kUnknown,
                                        [](EditorState& editor_arg, int delta) {
                                          editor_arg.AdvanceActiveBuffer(delta);
                                        }));

  editor_type->AddField(
      L"SetVariablePrompt",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, std::wstring variable) {
                        SetVariableCommandHandler(variable, editor_arg);
                      }));

  editor_type->AddField(
      L"home",
      vm::NewCallback(pool, PurityType::kPure, [](EditorState& editor_arg) {
        return editor_arg.home_directory().read();
      }));

  editor_type->AddField(
      L"pop_repetitions",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        auto value_arg = static_cast<int>(editor_arg.repetitions().value_or(1));
        editor_arg.ResetRepetitions();
        return value_arg;
      }));

  // Define one version for pure functions and one for non-pure, and adjust the
  // purity of this one.
  editor_type->AddField(
      L"ForEachActiveBuffer",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype})},
          [&pool = pool](std::vector<gc::Root<vm::Value>> input,
                         Trampoline& trampoline) {
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            NonNull<std::shared_ptr<PossibleError>> output;
            return editor_arg
                .ForEachActiveBuffer([callback = input[1].ptr()->LockCallback(),
                                      &trampoline, output](OpenBuffer& buffer) {
                  std::vector<gc::Root<vm::Value>> args;
                  args.push_back(
                      VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
                          trampoline.pool(), buffer.NewRoot()));
                  return callback(std::move(args), trampoline)
                      .Transform([](EvaluationOutput) { return Success(); })
                      .ConsumeErrors([output](Error error) {
                        output.value() = error;
                        return futures::Past(EmptyValue());
                      });
                })
                .Transform([output, &pool](EmptyValue) {
                  return std::visit(
                      overload{[](Error error) {
                                 return ValueOrError<EvaluationOutput>(error);
                               },
                               [&pool](EmptyValue) {
                                 return Success(EvaluationOutput::Return(
                                     vm::Value::NewVoid(pool)));
                               }},
                      output.value());
                });
          }));

  editor_type->AddField(
      L"ForEachActiveBufferWithRepetitions",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMType::Function(
               {VMType::Void(),
                VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype})},
          [&pool = pool](std::vector<gc::Root<vm::Value>> input,
                         Trampoline& trampoline) {
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            return editor_arg
                .ForEachActiveBufferWithRepetitions(
                    [callback = input[1].ptr()->LockCallback(),
                     &trampoline](OpenBuffer& buffer) {
                      std::vector<gc::Root<vm::Value>> args;
                      args.push_back(
                          VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
                              trampoline.pool(), buffer.NewRoot()));
                      return callback(std::move(args), trampoline)
                          .Transform([](EvaluationOutput) { return Success(); })
                          // TODO(easy): Don't ConsumeErrors; change
                          // ForEachActiveBuffer.
                          .ConsumeErrors([](Error) {
                            return futures::Past(EmptyValue());
                          });
                    })
                .Transform([&pool](EmptyValue) {
                  return EvaluationOutput::Return(vm::Value::NewVoid(pool));
                });
          }));

  editor_type->AddField(L"ProcessInput",
                        vm::NewCallback(pool, PurityType::kUnknown,
                                        [](EditorState& editor_arg, int c) {
                                          editor_arg.ProcessInput(c);
                                        }));

  editor_type->AddField(
      L"ConnectTo",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype, VMType::String()},
          [&pool = pool](std::vector<gc::Root<vm::Value>> args, Trampoline&)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            FUTURES_ASSIGN_OR_RETURN(
                Path target_path,
                editor_arg.status().LogErrors(AugmentErrors(
                    L"ConnectTo error",
                    Path::FromString(args[1].ptr()->get_string()))));
            OpenServerBuffer(editor_arg, target_path);
            return futures::Past(
                EvaluationOutput::Return(vm::Value::NewVoid(pool)));
          }));

  editor_type->AddField(
      L"WaitForClose",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype,
           VMTypeMapper<
               NonNull<std::shared_ptr<std::set<std::wstring>>>>::vmtype},
          [&pool = pool](std::vector<gc::Root<vm::Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 2u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            const auto& buffers_to_wait =
                VMTypeMapper<NonNull<std::shared_ptr<std::set<std::wstring>>>>::
                    get(args[1].ptr().value());

            auto values =
                std::make_shared<std::vector<futures::Value<EmptyValue>>>();
            for (const auto& buffer_name_str : buffers_to_wait.value()) {
              if (auto buffer_it =
                      editor_arg.buffers()->find(BufferName(buffer_name_str));
                  buffer_it != editor_arg.buffers()->end()) {
                values->push_back(buffer_it->second.ptr()->NewCloseFuture());
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
                  return EvaluationOutput::Return(vm::Value::NewVoid(pool));
                });
          }));

  editor_type->AddField(
      L"SendExitTo",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState&, std::wstring args) {
                        int fd = open(ToByteString(args).c_str(), O_WRONLY);
                        std::string command = "editor.Exit(0);\n";
                        write(fd, command.c_str(), command.size());
                        close(fd);
                      }));

  editor_type->AddField(L"Exit", vm::NewCallback(pool, PurityType::kUnknown,
                                                 [](EditorState&, int status) {
                                                   LOG(INFO)
                                                       << "Exit: " << status;
                                                   exit(status);
                                                 }));

  editor_type->AddField(
      L"SetStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, std::wstring s) {
                        editor_arg.status().SetInformationText(s);
                      }));

  editor_type->AddField(
      L"PromptAndOpenFile",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        NewOpenFileCommand(editor_arg)->ProcessInput(0);
      }));

  editor_type->AddField(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, bool value_arg) {
                        editor_arg.set_screen_needs_hard_redraw(value_arg);
                      }));

  editor_type->AddField(
      L"set_exit_value",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, int exit_value) {
                        editor_arg.set_exit_value(exit_value);
                      }));

  editor_type->AddField(
      L"ForkCommand",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg,
                         NonNull<std::shared_ptr<ForkCommandOptions>> options) {
                        return std::move(
                            ForkCommand(editor_arg, options.value()));
                      }));

  editor_type->AddField(
      L"repetitions",
      vm::NewCallback(pool, PurityType::kPure, [](EditorState& editor_arg) {
        // TODO: Somehow expose the optional to the VM.
        return static_cast<int>(editor_arg.repetitions().value_or(1));
      }));

  editor_type->AddField(L"set_repetitions",
                        vm::NewCallback(pool, PurityType::kUnknown,
                                        [](EditorState& editor_arg, int times) {
                                          editor_arg.set_repetitions(times);
                                        }));

  editor_type->AddField(
      L"OpenFile",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMTypeMapper<gc::Root<OpenBuffer>>::vmtype,
           VMTypeMapper<EditorState>::vmtype, VMType::String(), VMType::Bool()},
          [&pool = pool](std::vector<gc::Root<vm::Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 3u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            return OpenOrCreateFile(
                       OpenFileOptions{
                           .editor_state = editor_arg,
                           .path = OptionalFrom(
                               Path::FromString(args[1].ptr()->get_string())),
                           .insertion_type =
                               args[2].ptr()->get_bool()
                                   ? BuffersList::AddBufferType::kVisit
                                   : BuffersList::AddBufferType::kIgnore})
                .Transform([&pool](gc::Root<OpenBuffer> buffer) {
                  return EvaluationOutput::Return(
                      VMTypeMapper<gc::Root<OpenBuffer>>::New(pool, buffer));
                });
          }));

  editor_type->AddField(
      L"ShowInsertHistoryBuffer",
      vm::NewCallback(pool, PurityType::kUnknown, ShowInsertHistoryBuffer));

  editor_type->AddField(
      L"AddBinding",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), VMTypeMapper<EditorState>::vmtype, VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
          [&pool = pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            editor_arg.default_commands()->Add(
                args[1].ptr()->get_string(), args[2].ptr()->get_string(),
                std::move(args[3]), editor_arg.environment());
            return vm::Value::NewVoid(pool);
          }));

  RegisterBufferMethod(pool, editor_type.value(), L"ToggleActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"PushActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"PopActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"SetActiveCursorsToMarks",
                       vm::PurityTypeWriter,
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(pool, editor_type.value(), L"CreateCursor",
                       vm::PurityTypeWriter, &OpenBuffer::CreateCursor);
  RegisterBufferMethod(pool, editor_type.value(), L"DestroyCursor",
                       vm::PurityTypeWriter, &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(pool, editor_type.value(), L"DestroyOtherCursors",
                       vm::PurityTypeWriter, &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(pool, editor_type.value(), L"RepeatLastTransformation",
                       vm::PurityTypeWriter,
                       &OpenBuffer::RepeatLastTransformation);

  value.Define(L"editor",
               vm::Value::NewObject(
                   pool, editor_type->type().object_type,
                   NonNull<std::shared_ptr<EditorState>>::Unsafe(
                       std::shared_ptr<EditorState>(&editor, [](void*) {}))));

  value.DefineType(std::move(editor_type));

  value.DefineType(BuildBufferType(pool));

  InitShapes(pool, value);
  RegisterTransformations(pool, value);
  Modifiers::Register(pool, value);
  ForkCommandOptions::Register(pool, value);
  LineColumnRegister(pool, value);
  LineColumnDeltaRegister(pool, value);
  RangeRegister(pool, value);
  return environment;
}
}  // namespace afc::editor
