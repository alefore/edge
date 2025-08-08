#include "src/buffer_vm.h"

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/concurrent/protected.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/infrastructure/extended_char_vm.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/wstring.h"
#include "src/parse_tree.h"
#include "src/tests/tests.h"
#include "src/transformation/vm.h"
#include "src/vm/callbacks_gc.h"
#include "src/vm/constant_expression.h"
#include "src/vm/container.h"
#include "src/vm/function_call.h"

namespace gc = afc::language::gc;
namespace numbers = afc::math::numbers;
namespace container = afc::language::container;

using afc::concurrent::Protected;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::infrastructure::VectorExtendedChar;
using afc::infrastructure::screen::CursorsSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineMetadataKey;
using afc::language::text::LineMetadataMap;
using afc::language::text::LineMetadataValue;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineProcessorInput;
using afc::language::text::LineProcessorKey;
using afc::language::text::LineProcessorOutput;
using afc::language::text::LineProcessorOutputFuture;
using afc::language::text::LineSequence;
using afc::language::text::OutgoingLink;
using afc::language::text::Range;
using afc::vm::Environment;
using afc::vm::Identifier;
using afc::vm::kPurityTypeReader;
using afc::vm::kPurityTypeUnknown;
using afc::vm::NewConstantExpression;
using afc::vm::NewFunctionCall;
using afc::vm::ObjectType;
using afc::vm::PurityType;
using afc::vm::Trampoline;

namespace afc::vm {
template <>
const vm::types::ObjectName
    vm::VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::object_type_name{
        IDENTIFIER_CONSTANT(L"Buffer")};

/* static */ gc::Root<vm::Value>
VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  return VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(pool, value);
}

const vm::types::ObjectName
    vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::object_type_name =
        vm::types::ObjectName{IDENTIFIER_CONSTANT(L"Buffer")};

/* static */
NonNull<std::shared_ptr<
    concurrent::Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>
VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::get(Value& value) {
  return value.get_user_value<
      concurrent::Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>(
      object_type_name);
}

/* static */
gc::Root<Value> VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::
    New(gc::Pool& pool,
        NonNull<std::shared_ptr<
            concurrent::Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>
            input) {
  return vm::Value::NewObject(pool, object_type_name, input,
                              [input] { return Expand(input); });
}

/* static */ gc::Root<Value> VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::
    New(gc::Pool& pool,
        NonNull<std::shared_ptr<
            concurrent::Protected<std::vector<gc::Root<editor::OpenBuffer>>>>>
            input) {
  return input->lock([&pool](std::vector<gc::Root<editor::OpenBuffer>> roots) {
    return VMTypeMapper<NonNull<
        std::shared_ptr<Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::
        New(pool, MakeNonNullShared<
                      Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>(
                      concurrent::MakeProtected(
                          language::container::MaterializeVector(
                              roots | gc::view::Ptr))));
  });
}

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::object_type_name =
    types::ObjectName{IDENTIFIER_CONSTANT(L"VectorBuffer")};

/* static */ gc::Root<Value> VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::vector<gc::Root<editor::OpenBuffer>>>>>>::
    New(gc::Pool& pool,
        NonNull<std::shared_ptr<
            concurrent::Protected<std::vector<gc::Root<editor::OpenBuffer>>>>>
            input) {
  return VMTypeMapper<NonNull<std::shared_ptr<
      Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::New(pool,
                                                                  std::move(
                                                                      input));
}

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<gc::Root<editor::OpenBuffer>>>>>>::object_type_name =
    types::ObjectName{IDENTIFIER_CONSTANT(L"VectorBuffer")};
}  // namespace afc::vm

namespace afc::editor {
namespace {
template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    gc::Pool& pool, EdgeStruct* edge_struct, gc::Root<ObjectType>& object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  vm::Type buffer_type = object_type.ptr()->type();

  for (const vm::Identifier& name : edge_struct->VariableNames()) {
    const auto* variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type.ptr()->AddField(
        variable->name(),
        vm::NewCallback(pool, kPurityTypeReader,
                        [reader, variable](gc::Ptr<OpenBuffer> buffer) {
                          DVLOG(4) << "Buffer field reader is returning.";
                          return (buffer.value().*reader)(variable);
                        })
            .ptr());

    // Setter.
    object_type.ptr()->AddField(
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"set_") +
                   variable->name().read()},
        vm::NewCallback(
            pool, kPurityTypeUnknown,
            [variable, setter](gc::Ptr<OpenBuffer> buffer, FieldValue value) {
              (buffer.value().*setter)(variable, value);
            })
            .ptr());
  }
}

gc::Ptr<OpenBuffer> MaybeFollowOutgoingLink(gc::Ptr<OpenBuffer> buffer) {
  if (buffer->editor().structure() == Structure::kLine) {
    return VisitPointer(
        buffer->CurrentLine().outgoing_link(),
        [&](const OutgoingLink& link) {
          if (std::optional<gc::Root<OpenBuffer>> link_buffer =
                  buffer->editor().buffer_registry().FindPath(link.path);
              link_buffer.has_value())
            return link_buffer->ptr();
          return buffer;
        },
        [&] { return buffer; });
  }
  return buffer;
}

std::pair<LineNumber, LineNumberDelta> GetBoundariesForTransformation(
    const CursorsSet& cursors, const LineSequence& buffer) {
  CHECK(!cursors.empty());
  LineNumber position = cursors.active()->line;

  std::set<LineNumber> lines;
  std::transform(cursors.begin(), cursors.end(),
                 std::inserter(lines, lines.end()),
                 [](const LineColumn& p) { return p.line; });

  std::pair<LineNumber, LineNumberDelta> output;

  if (auto last_before =
          std::find_if(lines.rbegin(), lines.rend(),
                       [&](LineNumber p) { return p < position; });
      last_before != lines.rend())
    output = std::make_pair(*last_before,
                            position - *last_before + LineNumberDelta(1));
  else if (auto first_after =
               std::find_if(lines.begin(), lines.end(),
                            [&](LineNumber p) { return p > position; });
           first_after != lines.end())
    output =
        std::make_pair(position, *first_after - position + LineNumberDelta(1));
  else {
    output = std::make_pair(LineNumber(), buffer.size());
    // Skip the tail of empty lines.
    while (!output.second.IsZero() &&
           buffer.at(output.first + output.second - LineNumberDelta(1))
               .contents()
               .size()
               .IsZero())
      --output.second;
  }

  CHECK_GE(output.second, LineNumberDelta());
  CHECK_LT(output.first.ToDelta(), buffer.size());
  CHECK_LE((output.first + output.second).ToDelta(), buffer.size());
  return output;
}

template <typename KeyType>
void DefineSortLinesByKey(
    gc::Pool& pool, gc::Root<ObjectType>& buffer_object_type,
    vm::Type vm_type_key,
    std::function<ValueOrError<KeyType>(const vm::Value&)> get_key) {
  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"SortLinesByKey"),
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm_type_key},
                               .inputs = {vm::types::Number{}}}},
          [get_key](std::vector<gc::Root<vm::Value>> args,
                    Trampoline& trampoline) {
            CHECK_EQ(args.size(), size_t(2));

            struct Data {
              Trampoline& trampoline;
              gc::Ptr<OpenBuffer> buffer;
              PossibleError possible_error = Success();
              gc::Root<vm::Value> callback;
              std::unordered_map<LazyString, KeyType> keys = {};
            };

            const auto data = MakeNonNullShared<Data>(
                Data{.trampoline = trampoline,
                     .buffer = vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                         args[0].ptr().value()),
                     .callback = std::move(args[1])});

            const std::pair<LineNumber, LineNumberDelta> boundaries =
                GetBoundariesForTransformation(
                    data->buffer->active_cursors(),
                    data->buffer->contents().snapshot());

            LOG(INFO) << "Sorting with boundaries: " << boundaries.first << " "
                      << boundaries.second;
            // We build `inputs` simply to be able to use futures::ForEach.
            NonNull<std::shared_ptr<std::vector<LineNumber>>> inputs;
            Range{LineColumn{boundaries.first},
                  LineColumn{boundaries.first + boundaries.second}}
                .ForEachLine([&inputs](LineNumber number) {
                  inputs->push_back(number);
                  return true;
                });

            return futures::ForEach(
                       inputs.get_shared(),
                       [data, get_key](LineNumber line_number) {
                         return data->callback.ptr()
                             ->RunFunction({vm::Value::NewNumber(
                                               data->trampoline.pool(),
                                               numbers::Number::FromSizeT(
                                                   line_number.read()))},
                                           data->trampoline)
                             .Transform(
                                 [data, get_key,
                                  line_number](gc::Root<vm::Value> output)
                                     -> ValueOrError<
                                         futures::IterationControlCommand> {
                                   Line line =
                                       data->buffer->contents().at(line_number);
                                   ASSIGN_OR_RETURN(
                                       auto key_value,
                                       get_key(output.ptr().value()));
                                   data->keys.insert(
                                       {line.contents().read(), key_value});
                                   return Success(
                                       futures::IterationControlCommand::
                                           kContinue);
                                 })
                             .ConsumeErrors([data](Error error_input) {
                               data->possible_error = error_input;
                               return futures::Past(
                                   futures::IterationControlCommand::kStop);
                             });
                       })
                .Transform([data,
                            boundaries](futures::IterationControlCommand) {
                  return std::visit(
                      overload{
                          [](Error error) -> ValueOrError<gc::Root<vm::Value>> {
                            return error;
                          },
                          [data, boundaries](EmptyValue) {
                            data->buffer->SortContents(
                                boundaries.first, boundaries.second,
                                [data](const Line& a, const Line& b) {
                                  auto it_a =
                                      data->keys.find(a.contents().read());
                                  auto it_b =
                                      data->keys.find(b.contents().read());
                                  CHECK(it_a != data->keys.end());
                                  CHECK(it_b != data->keys.end());
                                  return it_a->second < it_b->second;
                                });
                            return Success(
                                vm::Value::NewVoid(data->trampoline.pool()));
                          }},
                      data->possible_error);
                });
          })
          .ptr());
}

futures::ValueOrError<language::gc::Root<vm::Value>> BufferForEach(
    Trampoline& trampoline, const LineSequence& contents,
    gc::Root<vm::Value> callback) {
  struct Data {
    Trampoline& trampoline;
    LineNumber line = LineNumber{};
    ValueOrError<language::gc::Root<vm::Value>> output;
    const gc::Root<vm::Value> callback;
    const LineSequence contents;
  };
  using ICC = futures::IterationControlCommand;
  auto data = MakeNonNullShared<Data>(
      Data{.trampoline = trampoline,
           .output = vm::Value::NewVoid(trampoline.pool()),
           .callback = callback,
           .contents = contents});
  return futures::While([data]() -> futures::Value<ICC> {
           if (data->line > data->contents.EndLine())
             return futures::Past(ICC::kStop);
           std::vector<language::gc::Root<vm::Value>> args{
               vm::Value::NewNumber(
                   data->trampoline.pool(),
                   math::numbers::Number::FromSizeT(data->line.read())),
               vm::Value::NewString(
                   data->trampoline.pool(),
                   ToLazyString(data->contents.at(data->line)))};
           ++data->line;
           return data->callback.ptr()
               ->RunFunction(std::move(args), data->trampoline)
               .Transform([data](gc::Root<vm::Value>) {
                 return futures::Past(ValueOrError<ICC>(ICC::kContinue));
               })
               .ConsumeErrors([data](Error error) {
                 data->output = error;
                 return futures::Past(ICC::kStop);
               });
         })
      .Transform([data](ICC) { return futures::Past(data->output); });
}
}  // namespace

void DefineBufferType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> buffer_object_type = ObjectType::New(
      pool, vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::object_type_name);

  RegisterBufferFields<EdgeStruct<bool>, bool>(
      pool, buffer_variables::BoolStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<LazyString>, LazyString>(
      pool, buffer_variables::StringStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<int>, int>(
      pool, buffer_variables::IntStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<double>, double>(
      pool, buffer_variables::DoubleStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<language::text::LineColumn>, LineColumn>(
      pool, buffer_variables::LineColumnStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"SetStatus"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](gc::Ptr<OpenBuffer> buffer, LazyString s) -> PossibleError {
            DECLARE_OR_RETURN(SingleLine line, SingleLine::New(s));
            buffer->status().SetInformationText(
                LineBuilder{std::move(line)}.Build());
            return EmptyValue{};
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"SetWarningStatus"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](gc::Ptr<OpenBuffer> buffer, LazyString s) {
                        buffer->status().InsertError(Error{s});
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"child_exit_status"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return static_cast<int>(buffer->child_exit_status().value_or(0));
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"tostring"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return ToSingleLine(buffer->name()).read().read();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"line_count"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return static_cast<int>(buffer->contents().size().read());
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"set_position"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](gc::Ptr<OpenBuffer> buffer, LineColumn position) {
                        buffer->set_position(position);
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"position"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return LineColumn(buffer->position());
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"active_cursors"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        const CursorsSet& cursors = buffer->active_cursors();
        return MakeNonNullShared<Protected<std::vector<LineColumn>>>(
            std::vector<LineColumn>(cursors.begin(), cursors.end()));
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"set_active_cursors"),
      vm::NewCallback(
          pool, kPurityTypeReader,
          [](gc::Ptr<OpenBuffer> buffer,
             NonNull<std::shared_ptr<Protected<std::vector<LineColumn>>>>
                 cursors) {
            cursors->lock([&](std::vector<LineColumn> values) {
              buffer->set_active_cursors(values);
            });
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"line"),
      vm::NewCallback(pool, kPurityTypeReader,
                      [](gc::Ptr<OpenBuffer> buffer, int line_input) {
                        LineNumber line =
                            std::min(LineNumber(std::max(line_input, 0)),
                                     LineNumber(0) + buffer->lines_size() -
                                         LineNumberDelta(1));
                        return buffer->contents().at(line).contents().read();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"ForEach"),
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{
               .output = {vm::types::Void{}},
               .inputs = {vm::types::Number{}, vm::types::String{}}}},
          [](std::vector<gc::Root<vm::Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 2ul);
            return BufferForEach(trampoline,
                                 vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                                     args[0].ptr().value())
                                     ->contents()
                                     .snapshot(),
                                 args[1]);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"ForEachWithRange"),
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::VMTypeMapper<language::text::Range>::object_type_name,
           vm::types::Function{.output = {vm::types::Void{}},
                               .inputs =
                                   {
                                       vm::types::Number{},
                                       vm::types::String{},
                                   }}},
          [](std::vector<gc::Root<vm::Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 3ul);
            return BufferForEach(trampoline,
                                 vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                                     args[0].ptr().value())
                                     ->contents()
                                     .snapshot()
                                     .ViewRange(vm::VMTypeMapper<Range>::get(
                                         args[1].ptr().value())),
                                 args[2]);
          })
          .ptr());

  DefineSortLinesByKey<numbers::Number>(
      pool, buffer_object_type, vm::types::Number{},
      [](const vm::Value& value) { return value.get_number(); });

  DefineSortLinesByKey<LazyString>(
      pool, buffer_object_type, vm::types::String{},
      [](const vm::Value& value) { return Success(value.get_string()); });

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"tree"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return buffer->parse_tree();
      }).ptr());

  // This isn't kPurityTypeReader because it has the side-effect of potentially
  // creating the directory.
  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"state_directory"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        return buffer->GetEdgeStateDirectory();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"ApplyTransformation"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](gc::Ptr<OpenBuffer> buffer,
             NonNull<std::shared_ptr<editor::transformation::Variant>>
                 transformation) {
            return buffer->ApplyToCursors(transformation.value());
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"PushTransformationStack"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer->PushTransformationStack();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"PopTransformationStack"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer->PopTransformationStack();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"Filter"),
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm::types::String{}},
                               .inputs = {vm::types::String{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            auto buffer = vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                args[0].ptr().value());
            buffer->set_filter(std::move(args[1]));
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"Reload"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        buffer->Reload();
        buffer->editor().ResetModifiers();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"SendEndOfFileToProcess"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        buffer->SendEndOfFileToProcess();
        buffer->editor().ResetModifiers();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"Save"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        futures::Value<PossibleError> output =
            buffer->Save(OpenBuffer::Options::SaveType::kMainFile);
        buffer->editor().ResetModifiers();
        return output;
      }).ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"Close"),
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [](gc::Ptr<OpenBuffer> buffer) {
                        buffer = MaybeFollowOutgoingLink(std::move(buffer));
                        buffer->editor().CloseBuffer(buffer.value());
                        buffer->editor().ResetModifiers();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"AddBinding"),
      vm::Value::NewFunction(
          pool, PurityType{.writes_external_outputs = true}, vm::types::Void{},
          {buffer_object_type.ptr()->type(), vm::types::String{},
           vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::Void{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            gc::Ptr<OpenBuffer> buffer =
                vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                    args[0].ptr().value());
            buffer->default_commands()->Add(
                VectorExtendedChar(args[1].ptr()->get_string()),
                args[2].ptr()->get_string(), std::move(args[3]).ptr(),
                buffer->environment());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  // TODO(easy, 2024-05-29): When capturing `buffer`, maybe capture a weakptr
  // or ensure that we expand it somehow. Otherwise, it may get collected under
  // our feet. Probably can't happen in practice, but it would be good to use
  // the type system to ensure that.
  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"AddBindingToFile"),
      vm::NewCallback(
          pool, PurityType{.writes_external_outputs = true},
          [](gc::Ptr<OpenBuffer> buffer,
             NonNull<std::shared_ptr<Protected<std::vector<ExtendedChar>>>>
                 keys,
             LazyString path) {
            LOG(INFO) << "AddBindingToFile: " << path;
            return keys->lock([&](std::vector<ExtendedChar>& keys_values) {
              buffer->default_commands()->Add(
                  keys_values,
                  [buffer, path]() {
                    ResolvePathOptions<EmptyValue>::New(
                        buffer->editor(), MakeNonNullShared<FileSystemDriver>(
                                              buffer->editor().thread_pool()))
                        .Transform([buffer, path](
                                       ResolvePathOptions<EmptyValue> options) {
                          options.path = path;
                          return futures::OnError(
                              ResolvePath(std::move(options))
                                  .Transform([buffer, path](
                                                 ResolvePathOutput<EmptyValue>
                                                     results) {
                                    buffer->execution_context()->EvaluateFile(
                                        results.path);
                                    return Success();
                                  }),
                              [buffer, path](Error error) {
                                buffer->status().Set(AugmentError(
                                    LazyString{L"Unable to resolve: "} + path,
                                    std::move(error)));
                                return futures::Past(Success());
                              });
                        });
                  },
                  LazyString{L"Load file: "} + path);
            });
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"ShowTrackers"),
      vm::NewCallback(
          pool, PurityType{.writes_external_outputs = true},
          [](gc::Ptr<OpenBuffer> buffer) {
            buffer->AppendLines(container::MaterializeVector(
                Tracker::GetData() |
                std::views::transform([](Tracker::Data data) -> const Line {
                  return LineBuilder(SingleLine{LazyString{L"\""}} +
                                     SingleLine{LazyString{data.name}} +
                                     SingleLine{LazyString{L"\","}} +
                                     SingleLine{LazyString{
                                         std::to_wstring(data.executions)}} +
                                     SingleLine{LazyString{L","}} +
                                     SingleLine{LazyString{
                                         std::to_wstring(data.seconds)}} +
                                     SingleLine{LazyString{L","}} +
                                     SingleLine{LazyString{std::to_wstring(
                                         data.longest_seconds)}})
                      .Build();
                })));
            buffer->AppendLine(SingleLine{});
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"EvaluateFile"),
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [](gc::Ptr<OpenBuffer> buffer, Path path) {
                        buffer->execution_context()->EvaluateFile(
                            std::move(path));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"WaitForEndOfFile"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        return buffer->WaitForEndOfFile().Transform(
            [root_buffer = buffer.ToRoot()](EmptyValue) {
              return root_buffer;
            });
      }).ptr());

  environment.Define(
      IDENTIFIER_CONSTANT(L"WaitForEndOfFile"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<
              std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>
                 buffers) {
            // We ignore the return values (they are EmptyValue anyway) and just
            // return `buffers` when the futures are all done.
            return futures::UnwrapVectorFuture(
                       MakeNonNullShared<
                           std::vector<futures::Value<EmptyValue>>>(
                           container::MaterializeVector(
                               *buffers->lock() |
                               std::views::transform(
                                   [](gc::Ptr<OpenBuffer>& buffer) {
                                     return buffer->WaitForEndOfFile();
                                   }))))
                .Transform([buffers](auto) { return buffers; });
          }));

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"LineMetadataString"),
      vm::NewCallback(
          pool, kPurityTypeReader,
          [](gc::Ptr<OpenBuffer> buffer,
             int line_number) -> futures::ValueOrError<LazyString> {
            LineMetadataMap metadata_map =
                buffer->contents().at(LineNumber(line_number)).metadata().get();
            if (const auto metadata_it = metadata_map.find(LineMetadataKey{});
                metadata_it != metadata_map.end())
              return metadata_it->second.value.ToFuture().Transform(
                  [](SingleLine a) { return a.read(); });
            return futures::Past(Error{LazyString{L"Line has no value."}});
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      IDENTIFIER_CONSTANT(L"AddLineProcessor"),
      vm::Value::NewFunction(
          pool, PurityType{.writes_external_outputs = true}, vm::types::Void{},
          {buffer_object_type.ptr()->type(), vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::String{}},
                               .inputs = {vm::types::String{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args,
                  Trampoline&) -> futures::ValueOrError<gc::Root<vm::Value>> {
            CHECK_EQ(args.size(), 3u);
            gc::Ptr<OpenBuffer> buffer =
                vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                    args[0].ptr().value());
            FUTURES_ASSIGN_OR_RETURN(LineProcessorKey key,
                                     LineProcessorKey::New(SingleLine::New(
                                         args[1].ptr()->get_string())));
            buffer->AddLineProcessor(
                key, [buffer,
                      callback = std::move(args[2])](LineProcessorInput input) {
                  return Success(LineProcessorOutputFuture{
                      .initial_value =
                          LineProcessorOutput{SINGLE_LINE_CONSTANT(L"…")},
                      .value =
                          buffer
                              ->EvaluateExpression(
                                  NewFunctionCall(
                                      NewConstantExpression(callback).ptr(),
                                      {NewConstantExpression(
                                           vm::Value::NewString(
                                               buffer->editor().gc_pool(),
                                               input.read()))
                                           .ptr()})
                                      .ptr(),
                                  buffer->environment().ToRoot())
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
                              })});
                });
            return futures::Past(vm::Value::NewVoid(pool));
          })
          .ptr());

  environment.DefineType(buffer_object_type.ptr());
  vm::container::Export<std::vector<gc::Ptr<OpenBuffer>>>(pool, environment);
}
namespace {
bool vector_tests_register = afc::tests::Register(
    L"VMVectorBuffer",
    {{.name = L"VectorPtr",
      .callback =
          [] {
            auto vector_buffer = MakeNonNullShared<
                Protected<std::vector<gc::Ptr<OpenBuffer>>>>();
            NonNull<std::unique_ptr<EditorState>> editor =
                EditorForTests(std::nullopt);
            std::optional<gc::Root<OpenBuffer>> buffer =
                NewBufferForTests(editor.value());
            editor->CloseBuffer(buffer.value().value());
            gc::WeakPtr<OpenBuffer> weak_buffer = buffer->ptr().ToWeakPtr();
            vector_buffer->lock()->push_back(buffer->ptr());

            std::optional<gc::Root<vm::Value>> value = vm::VMTypeMapper<NonNull<
                std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>>::
                New(editor->gc_pool(), std::move(vector_buffer));

            buffer = std::nullopt;
            editor->gc_pool().FullCollect();
            editor->gc_pool().BlockUntilDone();
            CHECK(weak_buffer.Lock().has_value());

            value = std::nullopt;
            editor->gc_pool().FullCollect();
            editor->gc_pool().BlockUntilDone();
            CHECK(!weak_buffer.Lock().has_value());
          }},
     {.name = L"FullEnvironment", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        std::vector<vm::Environment::LookupResult> factory =
            editor->execution_context()->environment()->PolyLookup(
                vm::Namespace{},
                Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"VectorBuffer")});
        CHECK_EQ(factory.size(), 1ul);
        Trampoline trampoline{Trampoline::Options{
            .pool = editor->gc_pool(),
            .environment = editor->execution_context()->environment().ToRoot(),
            .yield_callback = [](language::OnceOnlyFunction<void()>) {
              LOG(FATAL) << "Unexpected yield.";
            }}};
        gc::Root<vm::Value> vector_buffer =
            ValueOrDie(std::get<gc::Root<vm::Value>>(factory[0].value)
                           ->RunFunction({}, trampoline)
                           .Get()
                           .value());
        LOG(INFO) << "Convert.";
        NonNull<std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>
            typed_value = vm::VMTypeMapper<NonNull<
                std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>>::
                get(vector_buffer.value());
        LOG(INFO) << "Check size.";
        CHECK_EQ(typed_value->lock()->size(), 0ul);
      }}});
}  // namespace
}  // namespace afc::editor
