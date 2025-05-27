#include "src/editor_vm.h"

#include "src/buffer_vm.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/infrastructure/extended_char_vm.h"
#include "src/infrastructure/tracker.h"
#include "src/insert_history_buffer.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/open_file_command.h"
#include "src/parse_tree.h"
#include "src/run_command_handler.h"
#include "src/search_handler_vm.h"
#include "src/server.h"
#include "src/set_buffer_mode.h"
#include "src/set_variable_command.h"
#include "src/shapes.h"
#include "src/terminal.h"
#include "src/transformation/vm.h"
#include "src/vm/callbacks.h"
#include "src/vm/default_environment.h"
#include "src/vm/file_system.h"

namespace container = afc::language::container;

using afc::concurrent::MakeProtected;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::infrastructure::VectorExtendedChar;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::math::numbers::Number;
using afc::vm::Environment;
using afc::vm::GetVMType;
using afc::vm::Identifier;
using afc::vm::kPurityTypePure;
using afc::vm::kPurityTypeReader;
using afc::vm::kPurityTypeUnknown;
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
    VMTypeMapper<editor::EditorState>::object_type_name = types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Editor")}};
}  // namespace afc::vm

namespace afc::editor {

namespace gc = language::gc;
namespace {
template <typename MethodReturnType>
void RegisterBufferMethod(gc::Pool& pool, ObjectType& editor_type,
                          Identifier name, PurityType purity_type,
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

ValueOrError<int> FromVmValue(Number number) { return number.ToInt32(); }

template <typename EdgeStruct, typename FieldValue,
          typename FieldVmValue = FieldValue>
void RegisterVariableFields(
    gc::Pool& pool, EdgeStruct* edge_struct, afc::vm::ObjectType& editor_type,
    const FieldValue& (EditorState::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (EditorState::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  for (const vm::Identifier& name : edge_struct->VariableNames()) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    editor_type.AddField(
        variable->name(),
        vm::NewCallback(pool, kPurityTypeReader,
                        [reader, variable](EditorState& editor) {
                          return (editor.*reader)(variable);
                        })
            .ptr());

    // Setter.
    editor_type.AddField(
        Identifier{SingleLine{LazyString{L"set_"}} + variable->name().read()},
        vm::NewCallback(
            pool, kPurityTypeUnknown,
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

// TODO(2025-05-27, trivial): Move to futures?
// Turns a vector of futures into a future vector (of immediate values).
//
// std::vector<future::Value<X>>
// => future::Value<std::vector<X>>
template <typename Value>
futures::Value<std::vector<Value>> UnwrapVectorFuture(
    NonNull<std::shared_ptr<std::vector<futures::Value<Value>>>> input) {
  auto output = MakeNonNullShared<std::vector<Value>>();
  using futures::IterationControlCommand;
  // TODO(2025-05-27, trivial): Remove need to call `get_shared()` below:
  return futures::ForEach(
             input.get_shared(),
             [output](futures::Value<Value>& future_item) {
               return std::move(future_item).Transform([output](Value item) {
                 output->push_back(std::move(item));
                 return IterationControlCommand::kContinue;
               });
             })
      .Transform(
          [output](IterationControlCommand) mutable { return output.value(); });
}
}  // namespace

gc::Root<Environment> BuildEditorEnvironment(
    language::gc::Pool& pool,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver) {
  gc::Root<Environment> environment =
      Environment::New(afc::vm::NewDefaultEnvironment(pool).ptr());
  Environment& value = environment.ptr().value();
  using V = std::vector<ExtendedChar>;
  using PV = Protected<std::vector<ExtendedChar>>;
  using VSP = NonNull<std::shared_ptr<PV>>;
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_backspace"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kBackspace}))));
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_control_a"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kCtrlA}))));
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_control_e"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kCtrlE}))));
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_control_d"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kCtrlD}))));
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_control_k"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kCtrlK}))));
  value.Define(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"terminal_control_u"}}}},
      vm::Value::NewObject(
          pool, VMTypeMapper<VSP>::object_type_name,
          MakeNonNullShared<PV>(MakeProtected(V{ControlChar::kCtrlU}))));

  gc::Root<ObjectType> editor_type = ObjectType::New(
      pool, VMTypeMapper<editor::EditorState>::object_type_name);

  // Methods for Editor.
  RegisterVariableFields<EdgeStruct<bool>, bool>(
      pool, editor_variables::BoolStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<LazyString>, LazyString>(
      pool, editor_variables::StringStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  RegisterVariableFields<EdgeStruct<int>, int, Number>(
      pool, editor_variables::IntStruct(), editor_type.ptr().value(),
      &EditorState::Read, &EditorState::Set);

  editor_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ResetAllTrackers")},
      vm::NewCallback(pool, kPurityTypeUnknown, [](EditorState&) {
        Tracker::ResetAll();
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"EnterSetBufferMode"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](EditorState& editor_arg) {
        editor_arg.set_keyboard_redirect(NewSetBufferMode(editor_arg));
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"SetActiveBuffer"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, int delta) {
                        editor_arg.SetActiveBuffer(delta);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"AdvanceActiveBuffer"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, int delta) {
                        editor_arg.AdvanceActiveBuffer(delta);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"SetVariablePrompt"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState& editor_arg, LazyString input) -> PossibleError {
            DECLARE_OR_RETURN(SingleLine variable, SingleLine::New(input));
            SetVariableCommandHandler(editor_arg, variable);
            return Success();
          })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"home"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](EditorState& editor_arg) {
        return editor_arg.home_directory().read();
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"pop_repetitions"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](EditorState& editor_arg) {
        int value_arg = static_cast<int>(editor_arg.repetitions().value_or(1));
        editor_arg.ResetRepetitions();
        return value_arg;
      }).ptr());

  // Define one version for pure functions and one for non-pure, and adjust the
  // purity of this one.
  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"ForEachActiveBuffer"}}}},
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(),
           vm::types::Function{
               .output = vm::Type{vm::types::Void{}},
               .inputs =
                   {vm::GetVMType<gc::Ptr<editor::OpenBuffer>>::vmtype()}}},
          [&pool](std::vector<gc::Root<vm::Value>> input,
                  Trampoline& trampoline) {
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            NonNull<std::shared_ptr<PossibleError>> output;
            return editor_arg
                .ForEachActiveBuffer([callback = std::move(input[1]),
                                      &trampoline, output](OpenBuffer& buffer) {
                  std::vector<gc::Root<vm::Value>> args;
                  args.push_back(VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(
                      trampoline.pool(), buffer.NewRoot()));
                  return callback.ptr()
                      ->RunFunction(std::move(args), trampoline)
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
      Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"ForEachActiveBufferWithRepetitions"}}}},
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(),
           vm::types::Function{
               .output = vm::Type{vm::types::Void{}},
               .inputs = {GetVMType<gc::Ptr<editor::OpenBuffer>>::vmtype()}}},
          [&pool](std::vector<gc::Root<vm::Value>> input,
                  Trampoline& trampoline) {
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(input[0].ptr().value());
            return editor_arg
                .ForEachActiveBufferWithRepetitions([callback =
                                                         std::move(input[1]),
                                                     &trampoline](
                                                        OpenBuffer& buffer) {
                  std::vector<gc::Root<vm::Value>> args;
                  args.push_back(VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(
                      trampoline.pool(), buffer.NewRoot()));
                  return callback.ptr()
                      ->RunFunction(std::move(args), trampoline)
                      .Transform([](gc::Root<vm::Value>) { return Success(); })
                      // TODO(easy): Don't ConsumeErrors; change
                      // ForEachActiveBuffer.
                      .ConsumeErrors(
                          [](Error) { return futures::Past(EmptyValue()); });
                })
                .Transform(
                    [&pool](EmptyValue) { return vm::Value::NewVoid(pool); });
          })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"ProcessInput"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState& editor_arg, int c) { editor_arg.ProcessInput({c}); })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"ConnectTo"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, Path target_path)
                          -> futures::ValueOrError<EmptyValue> {
                        OpenServerBuffer(editor_arg, target_path);
                        return futures::Past(EmptyValue());
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"WaitForClose"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState&,
             NonNull<
                 std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>
                 buffers_to_wait) {
            return futures::ForEach(
                       std::make_shared<
                           std::vector<futures::Value<EmptyValue>>>(
                           buffers_to_wait->lock(
                               [](const std::vector<gc::Ptr<OpenBuffer>>&
                                      buffers) {
                                 return container::MaterializeVector(
                                     buffers |
                                     std::views::transform(
                                         [](gc::Ptr<OpenBuffer> buffer) {
                                           return buffer->NewCloseFuture();
                                         }));
                               })),
                       [](futures::Value<EmptyValue>& future) {
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
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"SendExitTo"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState&, LazyString args) {
                        int fd = open(args.ToBytes().c_str(), O_WRONLY);
                        std::string command = "editor.Exit(0);\n";
                        write(fd, command.c_str(), command.size());
                        close(fd);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"Exit"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](EditorState&, int status) {
        LOG(INFO) << "Exit: " << status;
        exit(status);
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"SetStatus"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState& editor_arg, LazyString s) -> PossibleError {
            DECLARE_OR_RETURN(SingleLine line, SingleLine::New(s));
            editor_arg.status().SetInformationText(
                LineBuilder{std::move(line)}.Build());
            return EmptyValue{};
          })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"PromptAndOpenFile"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](EditorState& editor_arg) {
        NewOpenFileCommand(editor_arg).ptr()->ProcessInput(0);
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"set_screen_needs_hard_redraw"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, bool value_arg) {
                        editor_arg.set_screen_needs_hard_redraw(value_arg);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_exit_value"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, int exit_value) {
                        editor_arg.set_exit_value(exit_value);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"ForkCommand"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg,
                         NonNull<std::shared_ptr<ForkCommandOptions>> options) {
                        return ForkCommand(editor_arg, options.value());
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"repetitions"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](EditorState& editor_arg) {
        // TODO: Somehow expose the optional to the VM.
        return static_cast<int>(editor_arg.repetitions().value_or(1));
      }).ptr());

  editor_type.ptr()->AddField(
      Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_repetitions"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](EditorState& editor_arg, int times) {
                        editor_arg.set_repetitions(times);
                      })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"OpenFile"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState& editor_arg, LazyString path_str,
             bool visit) -> futures::ValueOrError<gc::Root<OpenBuffer>> {
            return OpenOrCreateFile(OpenFileOptions{
                .editor_state = editor_arg,
                .path = OptionalFrom(Path::New(path_str)),
                .insertion_type = visit ? BuffersList::AddBufferType::kVisit
                                        : BuffersList::AddBufferType::kIgnore});
          })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"OpenFile"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](EditorState& editor_arg,
             NonNull<std::shared_ptr<Protected<std::vector<LazyString>>>>
                 protected_paths,
             bool visit)
              -> futures::Value<NonNull<std::shared_ptr<
                  Protected<std::vector<gc::Root<OpenBuffer>>>>>> {
            return protected_paths->lock([&editor_arg, visit](
                                             std::vector<LazyString> paths) {
              return UnwrapVectorFuture(
                         MakeNonNullShared<
                             std::vector<futures::Value<gc::Root<OpenBuffer>>>>(
                             container::MaterializeVector(
                                 paths |
                                 std::views::transform(
                                     [&editor_arg, visit](LazyString path)
                                         -> futures::Value<
                                             gc::Root<OpenBuffer>> {
                                       return OpenOrCreateFile(OpenFileOptions{
                                           .editor_state = editor_arg,
                                           .path =
                                               OptionalFrom(Path::New(path)),
                                           .insertion_type =
                                               visit ? BuffersList::
                                                           AddBufferType::kVisit
                                                     : BuffersList::
                                                           AddBufferType::
                                                               kIgnore});
                                     }))))
                  .Transform(
                      [](std::vector<gc::Root<OpenBuffer>> vector_buffer) {
                        return MakeNonNullShared<
                            Protected<std::vector<gc::Root<OpenBuffer>>>>(
                            MakeProtected(std::move(vector_buffer)));
                      });
            });
          })
          .ptr());

  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"ShowInsertHistoryBuffer"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, ShowInsertHistoryBuffer).ptr());

  // Version of AddBinding that receives a VectorExtendedChar.
  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"AddBinding"}}}},
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {GetVMType<EditorState>::vmtype(),
           VMTypeMapper<VSP>::object_type_name, vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::Void{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            EditorState& editor_arg =
                VMTypeMapper<EditorState>::get(args[0].ptr().value());
            editor_arg.default_commands().ptr()->Add(
                VMTypeMapper<VSP>::get(args[1].ptr().value())->lock([](V keys) {
                  return keys;
                }),
                args[2].ptr()->get_string(), std::move(args[3]),
                editor_arg.execution_context()->environment());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  // Version of AddBinding that receives a String.
  editor_type.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"AddBinding"}}}},
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
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
                editor_arg.execution_context()->environment());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{
                           SingleLine{LazyString{L"ToggleActiveCursors"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::ToggleActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{
                           SingleLine{LazyString{L"PushActiveCursors"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::PushActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{
                           SingleLine{LazyString{L"PopActiveCursors"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::PopActiveCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{
                           SingleLine{LazyString{L"SetActiveCursorsToMarks"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::SetActiveCursorsToMarks);
  RegisterBufferMethod(
      pool, editor_type.ptr().value(),
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"CreateCursor"}}}},
      PurityType{.writes_external_outputs = true}, &OpenBuffer::CreateCursor);
  RegisterBufferMethod(
      pool, editor_type.ptr().value(),
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"DestroyCursor"}}}},
      PurityType{.writes_external_outputs = true}, &OpenBuffer::DestroyCursor);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{
                           SingleLine{LazyString{L"DestroyOtherCursors"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::DestroyOtherCursors);
  RegisterBufferMethod(pool, editor_type.ptr().value(),
                       Identifier{NonEmptySingleLine{SingleLine{
                           LazyString{L"RepeatLastTransformation"}}}},
                       PurityType{.writes_external_outputs = true},
                       &OpenBuffer::RepeatLastTransformation);

  value.DefineType(editor_type.ptr());

  DefineBufferType(pool, value);

  InitShapes(pool, value);
  RegisterTransformations(pool, value);
  Modifiers::Register(pool, value);
  ForkCommandOptions::Register(pool, value);
  RegisterParseTreeFunctions(pool, value);
  RegisterSearchOptionsVm(pool, value);
  language::text::LineColumnRegister(pool, value);
  language::text::LineColumnDeltaRegister(pool, value);
  language::text::RangeRegister(pool, value);
  vm::RegisterFileSystemFunctions(pool, std::move(file_system_driver),
                                  environment.ptr().value());
  return environment;
}
}  // namespace afc::editor
