#include "src/editor_vm.h"

#include "src/buffer_vm.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/infrastructure/extended_char_vm.h"
#include "src/insert_history_buffer.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/open_file_command.h"
#include "src/parse_tree.h"
#include "src/run_command_handler.h"
#include "src/server.h"
#include "src/set_buffer_mode.h"
#include "src/set_variable_command.h"
#include "src/shapes.h"
#include "src/terminal.h"
#include "src/transformation/vm.h"
#include "src/vm/callbacks.h"
#include "src/vm/default_environment.h"
#include "src/vm/file_system.h"

using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::VectorExtendedChar;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::math::numbers::Number;
using afc::math::numbers::ToInt;
using afc::vm::Environment;
using afc::vm::GetVMType;
using afc::vm::ObjectType;
using afc::vm::PurityType;
using afc::vm::Trampoline;
using afc::vm::VMTypeMapper;

namespace afc::vm {
/* static */ editor::EditorState& VMTypeMapper<editor::EditorState>::get(
    Value& value) {
  return value.get_user_value<editor::EditorState>(object_type_name).value();
}

/* static */ const types::ObjectName
    VMTypeMapper<editor::EditorState>::object_type_name =
        types::ObjectName(L"Editor");
}  // namespace afc::vm

namespace afc::editor {

namespace gc = language::gc;
namespace {
template <typename MethodReturnType>
void RegisterBufferMethod(gc::Pool& pool, ObjectType& editor_type,
                          const std::wstring& name, PurityType purity_type,
                          MethodReturnType (OpenBuffer::*method)(void)) {
  editor_type.AddField(
      name, vm::NewCallback(pool, purity_type, [method](EditorState& editor) {
              return editor
                  .ForEachActiveBuffer([method](OpenBuffer& buffer) {
                    (buffer.*method)();
                    return futures::Past(EmptyValue());
                  })
                  .Transform([&editor](EmptyValue) -> PossibleError {
                    editor.ResetModifiers();
                    return EmptyValue();
                  });
            }).ptr());
}

template <typename T>
ValueOrError<T> FromVmValue(T t) {
  return t;
}

ValueOrError<int> FromVmValue(Number number) { return ToInt32(number); }

template <typename EdgeStruct, typename FieldValue,
          typename FieldVmValue = FieldValue>
void RegisterVariableFields(
    gc::Pool& pool, EdgeStruct* edge_struct, afc::vm::ObjectType& editor_type,
    const FieldValue& (EditorState::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (EditorState::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  for (const std::wstring& name : edge_struct->VariableNames()) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    editor_type.AddField(
        variable->name(),
        vm::NewCallback(pool, PurityType::kReader,
                        [reader, variable](EditorState& editor) {
                          return (editor.*reader)(variable);
                        })
            .ptr());

    // Setter.
    editor_type.AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            pool, PurityType::kUnknown,
            [variable, setter](EditorState& editor, FieldVmValue value)
                -> futures::Value<PossibleError> {
              FUTURES_ASSIGN_OR_RETURN(auto processed_value,
                                       FromVmValue(value));
              (editor.*setter)(variable, processed_value);
              return futures::Past(Success());
            })
            .ptr());
  }
}
}  // namespace

gc::Root<Environment> BuildEditorEnvironment(
    language::gc::Pool& pool,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver) {
  gc::Root<Environment> environment =
      Environment::New(afc::vm::NewDefaultEnvironment(pool).ptr());
  Environment& value = environment.ptr().value();
  using V = std::vector<ExtendedChar>;
  using VS = NonNull<std::shared_ptr<V>>;
  value.Define(
      L"terminal_backspace",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kBackspace})));
  value.Define(
      L"terminal_control_a",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kCtrlA})));
  value.Define(
      L"terminal_control_e",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kCtrlE})));
  value.Define(
      L"terminal_control_d",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kCtrlD})));
  value.Define(
      L"terminal_control_k",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kCtrlK})));
  value.Define(
      L"terminal_control_u",
      vm::Value::NewObject(pool, VMTypeMapper<VS>::object_type_name,
                           MakeNonNullShared<V>(V{ControlChar::kCtrlU})));

  gc::Root<ObjectType> editor_type = ObjectType::New(
      pool, VMTypeMapper<editor::EditorState>::object_type_name);

  // Methods for Editor.
  RegisterVariableFields<EdgeStruct<bool>, bool>(
      pool, editor_variables::BoolStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<std::wstring>, std::wstring>(
      pool, editor_variables::StringStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<int>, int, Number>(
      pool, editor_variables::IntStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  editor_type.ptr()->AddField(
      L"EnterSetBufferMode",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        editor_arg.set_keyboard_redirect(NewSetBufferMode(editor_arg));
      }).ptr());

  editor_type.ptr()->AddField(
      L"SetActiveBuffer",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, int delta) {
                        editor_arg.SetActiveBuffer(delta);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"AdvanceActiveBuffer",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, int delta) {
                        editor_arg.AdvanceActiveBuffer(delta);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"SetVariablePrompt",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, std::wstring variable) {
                        SetVariableCommandHandler(editor_arg,
                                                  NewLazyString(variable));
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"home",
      vm::NewCallback(pool, PurityType::kPure, [](EditorState& editor_arg) {
        return editor_arg.home_directory().read();
      }).ptr());

  editor_type.ptr()->AddField(
      L"pop_repetitions",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        int value_arg = static_cast<int>(editor_arg.repetitions().value_or(1));
        editor_arg.ResetRepetitions();
        return value_arg;
      }).ptr());

  // Define one version for pure functions and one for non-pure, and adjust the
  // purity of this one.
  editor_type.ptr()->AddField(
      L"ForEachActiveBuffer",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(),
           vm::types::Function{
               .output = vm::Type{vm::types::Void{}},
               .inputs =
                   {vm::GetVMType<gc::Root<editor::OpenBuffer>>::vmtype()}}},
          [&pool](std::vector<gc::Root<vm::Value>> input,
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
                  return callback.ptr()
                      ->value(std::move(args), trampoline)
                      .Transform([](gc::Root<vm::Value>) { return Success(); })
                      .ConsumeErrors([output](Error error) {
                        output.value() = error;
                        return futures::Past(EmptyValue());
                      });
                })
                .Transform([output, &pool](EmptyValue) {
                  return std::visit(
                      overload{
                          [](Error error) -> ValueOrError<gc::Root<vm::Value>> {
                            return error;
                          },
                          [&pool](EmptyValue) {
                            return Success(vm::Value::NewVoid(pool));
                          }},
                      output.value());
                });
          })
          .ptr());

  editor_type.ptr()->AddField(
      L"ForEachActiveBufferWithRepetitions",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(),
           vm::types::Function{
               .output = vm::Type{vm::types::Void{}},
               .inputs = {GetVMType<gc::Root<editor::OpenBuffer>>::vmtype()}}},
          [&pool](std::vector<gc::Root<vm::Value>> input,
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
                      return callback.ptr()
                          ->value(std::move(args), trampoline)
                          .Transform(
                              [](gc::Root<vm::Value>) { return Success(); })
                          // TODO(easy): Don't ConsumeErrors; change
                          // ForEachActiveBuffer.
                          .ConsumeErrors([](Error) {
                            return futures::Past(EmptyValue());
                          });
                    })
                .Transform(
                    [&pool](EmptyValue) { return vm::Value::NewVoid(pool); });
          })
          .ptr());

  editor_type.ptr()->AddField(
      L"ProcessInput", vm::NewCallback(pool, PurityType::kUnknown,
                                       [](EditorState& editor_arg, int c) {
                                         editor_arg.ProcessInput({c});
                                       })
                           .ptr());

  editor_type.ptr()->AddField(
      L"ConnectTo",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, Path target_path)
                          -> futures::ValueOrError<EmptyValue> {
                        OpenServerBuffer(editor_arg, target_path);
                        return futures::Past(EmptyValue());
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"WaitForClose",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](EditorState& editor_arg,
             NonNull<std::shared_ptr<std::set<std::wstring>>> buffers_to_wait) {
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
                         return std::move(future).Transform([](EmptyValue) {
                           return futures::IterationControlCommand::kContinue;
                         });
                       })
                .Transform([](futures::IterationControlCommand) {
                  return EmptyValue();
                });
          })
          .ptr());

  editor_type.ptr()->AddField(
      L"SendExitTo",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState&, std::wstring args) {
                        int fd = open(ToByteString(args).c_str(), O_WRONLY);
                        std::string command = "editor.Exit(0);\n";
                        write(fd, command.c_str(), command.size());
                        close(fd);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"Exit",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState&, int status) {
        LOG(INFO) << "Exit: " << status;
        exit(status);
      }).ptr());

  editor_type.ptr()->AddField(
      L"SetStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, std::wstring s) {
                        editor_arg.status().SetInformationText(Line(s));
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"PromptAndOpenFile",
      vm::NewCallback(pool, PurityType::kUnknown, [](EditorState& editor_arg) {
        NewOpenFileCommand(editor_arg).ptr()->ProcessInput(0);
      }).ptr());

  editor_type.ptr()->AddField(
      L"set_screen_needs_hard_redraw",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, bool value_arg) {
                        editor_arg.set_screen_needs_hard_redraw(value_arg);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"set_exit_value",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, int exit_value) {
                        editor_arg.set_exit_value(exit_value);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"ForkCommand",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg,
                         NonNull<std::shared_ptr<ForkCommandOptions>> options) {
                        return ForkCommand(editor_arg, options.value());
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"repetitions",
      vm::NewCallback(pool, PurityType::kPure, [](EditorState& editor_arg) {
        // TODO: Somehow expose the optional to the VM.
        return static_cast<int>(editor_arg.repetitions().value_or(1));
      }).ptr());

  editor_type.ptr()->AddField(
      L"set_repetitions",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](EditorState& editor_arg, int times) {
                        editor_arg.set_repetitions(times);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      L"OpenFile",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](EditorState& editor_arg, std::wstring path_str,
             bool visit) -> futures::ValueOrError<gc::Root<OpenBuffer>> {
            return OpenOrCreateFile(OpenFileOptions{
                .editor_state = editor_arg,
                .path = OptionalFrom(Path::FromString(path_str)),
                .insertion_type = visit ? BuffersList::AddBufferType::kVisit
                                        : BuffersList::AddBufferType::kIgnore});
          })
          .ptr());

  editor_type.ptr()->AddField(
      L"ShowInsertHistoryBuffer",
      vm::NewCallback(pool, PurityType::kUnknown, ShowInsertHistoryBuffer)
          .ptr());

  // Version of AddBinding that receives a VectorExtendedChar.
  editor_type.ptr()->AddField(
      L"AddBinding",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(), VMTypeMapper<VS>::object_type_name,
           vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::Void{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            editor_arg.default_commands().ptr()->Add(
                VMTypeMapper<VS>::get(args[1].ptr().value()).value(),
                args[2].ptr()->get_string(), std::move(args[3]),
                editor_arg.environment().ptr());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  // Version of AddBinding that receives a String.
  editor_type.ptr()->AddField(
      L"AddBinding",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(), vm::types::String{},
           vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::Void{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            editor_arg.default_commands().ptr()->Add(
                VectorExtendedChar(args[1].ptr()->get_string()),
                args[2].ptr()->get_string(), std::move(args[3]),
                editor_arg.environment().ptr());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  RegisterBufferMethod(pool, editor_type.ptr().value(), L"ToggleActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(), L"PushActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(), L"PopActiveCursors",
                       vm::PurityTypeWriter, &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       L"SetActiveCursorsToMarks", vm::PurityTypeWriter,
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(pool, editor_type.ptr().value(), L"CreateCursor",
                       vm::PurityTypeWriter, &OpenBuffer::CreateCursor);
  RegisterBufferMethod(pool, editor_type.ptr().value(), L"DestroyCursor",
                       vm::PurityTypeWriter, &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(pool, editor_type.ptr().value(), L"DestroyOtherCursors",
                       vm::PurityTypeWriter, &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       L"RepeatLastTransformation", vm::PurityTypeWriter,
                       &OpenBuffer::RepeatLastTransformation);

  value.DefineType(editor_type.ptr());

  value.DefineType(BuildBufferType(pool).ptr());

  InitShapes(pool, value);
  RegisterTransformations(pool, value);
  Modifiers::Register(pool, value);
  ForkCommandOptions::Register(pool, value);
  RegisterParseTreeFunctions(pool, value);
  language::text::LineColumnRegister(pool, value);
  language::text::LineColumnDeltaRegister(pool, value);
  language::text::RangeRegister(pool, value);
  vm::RegisterFileSystemFunctions(pool, std::move(file_system_driver),
                                  environment.ptr().value());
  return environment;
}
}  // namespace afc::editor
