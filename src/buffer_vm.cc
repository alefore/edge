#include "src/buffer_vm.h"

#include "src/buffer.h"
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
#include "src/parse_tree.h"
#include "src/transformation/vm.h"
#include "src/vm/container.h"

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
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NewError;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::OutgoingLink;
using afc::language::text::Range;
using afc::vm::Environment;
using afc::vm::Identifier;
using afc::vm::kPurityTypeReader;
using afc::vm::kPurityTypeUnknown;
using afc::vm::ObjectType;
using afc::vm::PurityType;
using afc::vm::Trampoline;

namespace afc::vm {
struct BufferWrapper {
  const gc::Ptr<editor::OpenBuffer> buffer;
};

gc::Ptr<editor::OpenBuffer> vm::VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::get(
    Value& value) {
  BufferWrapper wrapper =
      value.get_user_value<BufferWrapper>(object_type_name).value();
  return wrapper.buffer;
}

/* static */ gc::Root<vm::Value> VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Ptr<editor::OpenBuffer> value) {
  return vm::Value::NewObject(
      pool, object_type_name,
      MakeNonNullShared<BufferWrapper>(BufferWrapper{.buffer = value}),
      [object_metadata = value.object_metadata()] {
        return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
            {object_metadata});
      });
}

/* static */ gc::Root<vm::Value> VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  return New(pool, value.ptr());
}

const vm::types::ObjectName
    vm::VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::object_type_name =
        vm::types::ObjectName(L"Buffer");

/* static */ gc::Root<vm::Value>
VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  return VMTypeMapper<gc::Ptr<editor::OpenBuffer>>::New(pool, value);
}

const vm::types::ObjectName
    vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::object_type_name =
        vm::types::ObjectName(L"Buffer");

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<gc::Ptr<editor::OpenBuffer>>>>>>::object_type_name =
    types::ObjectName(L"VectorBuffer");
}  // namespace afc::vm

namespace afc::editor {

namespace gc = language::gc;
namespace {
template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    gc::Pool& pool, EdgeStruct* edge_struct, gc::Root<ObjectType>& object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  vm::Type buffer_type = object_type.ptr()->type();

  for (const std::wstring& name : edge_struct->VariableNames()) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type.ptr()->AddField(
        Identifier(variable->name()),
        vm::NewCallback(pool, kPurityTypeReader,
                        [reader, variable](gc::Ptr<OpenBuffer> buffer) {
                          DVLOG(4) << "Buffer field reader is returning.";
                          return (buffer.value().*reader)(variable);
                        })
            .ptr());

    // Setter.
    object_type.ptr()->AddField(
        Identifier(L"set_" + variable->name()),
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
          if (auto it = buffer->editor().buffers()->find(BufferName(link.path));
              it != buffer->editor().buffers()->end())
            return it->second.ptr();
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
      Identifier(L"SortLinesByKey"),
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
              gc::Root<gc::ValueWithFixedDependencies<vm::Value::Callback>>
                  callback;
              std::unordered_map<std::wstring, KeyType> keys = {};
            };

            const auto data = MakeNonNullShared<Data>(
                Data{.trampoline = trampoline,
                     .buffer = vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                         args[0].ptr().value()),
                     .callback = args[1].ptr()->LockCallback()});

            const std::pair<LineNumber, LineNumberDelta> boundaries =
                GetBoundariesForTransformation(
                    data->buffer->active_cursors(),
                    data->buffer->contents().snapshot());

            LOG(INFO) << "Sorting with boundaries: " << boundaries.first << " "
                      << boundaries.second;
            // We build `inputs` simply to be able to use futures::ForEach.
            NonNull<std::shared_ptr<std::vector<LineNumber>>> inputs;
            data->buffer->contents().snapshot().ForEachLine(
                boundaries.first, boundaries.second,
                [&inputs](LineNumber number, const Line&) {
                  inputs->push_back(number);
                  return true;
                });

            return futures::ForEach(
                       inputs.get_shared(),
                       [data, get_key](LineNumber line_number) {
                         return data->callback.ptr()
                             ->value({vm::Value::NewNumber(
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
                                   VLOG(9) << "Value for line: " << line << ": "
                                           << get_key(output.ptr().value());
                                   ASSIGN_OR_RETURN(
                                       auto key_value,
                                       get_key(output.ptr().value()));
                                   data->keys.insert(
                                       {line.ToString(), key_value});
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
                                      data->keys.find(a.contents().ToString());
                                  auto it_b =
                                      data->keys.find(b.contents().ToString());
                                  CHECK(it_a != data->keys.end());
                                  CHECK(it_b != data->keys.end());
                                  VLOG(10)
                                      << "Sort key: " << a.contents().ToString()
                                      << ": " << it_a->second;
                                  VLOG(10)
                                      << "Sort key: " << b.contents().ToString()
                                      << ": " << it_b->second;
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
}  // namespace

void DefineBufferType(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> buffer_object_type = ObjectType::New(
      pool, vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::object_type_name);

  RegisterBufferFields<EdgeStruct<bool>, bool>(
      pool, buffer_variables::BoolStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<std::wstring>, std::wstring>(
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
      Identifier(L"SetStatus"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](gc::Ptr<OpenBuffer> buffer, LazyString s) {
                        buffer->status().SetInformationText(
                            LineBuilder{s}.Build());
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"SetWarningStatus"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](gc::Ptr<OpenBuffer> buffer, LazyString s) {
                        buffer->status().InsertError(NewError(s));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"child_exit_status"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return static_cast<int>(buffer->child_exit_status().value_or(0));
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"tostring"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return to_wstring(buffer->name());
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"line_count"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return static_cast<int>(buffer->contents().size().read());
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"set_position"),
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](gc::Ptr<OpenBuffer> buffer, LineColumn position) {
                        buffer->set_position(position);
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"position"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return LineColumn(buffer->position());
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"active_cursors"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        const CursorsSet& cursors = buffer->active_cursors();
        return MakeNonNullShared<Protected<std::vector<LineColumn>>>(
            std::vector<LineColumn>(cursors.begin(), cursors.end()));
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"set_active_cursors"),
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
      Identifier(L"line"),
      vm::NewCallback(pool, kPurityTypeReader,
                      [](gc::Ptr<OpenBuffer> buffer, int line_input) {
                        LineNumber line =
                            std::min(LineNumber(std::max(line_input, 0)),
                                     LineNumber(0) + buffer->lines_size() -
                                         LineNumberDelta(1));
                        return buffer->contents().at(line).ToString();
                      })
          .ptr());

  // TODO(2024-03-14, trivial): Sort by Number.
  DefineSortLinesByKey<int64_t>(
      pool, buffer_object_type, vm::types::Number{},
      [](const vm::Value& value) { return value.get_number().ToInt64(); });

  DefineSortLinesByKey<std::wstring>(
      pool, buffer_object_type, vm::types::String{},
      [](const vm::Value& value) {
        // TODO(2024-01-24): Get rid of call to ToString.
        return Success(value.get_string().ToString());
      });

  buffer_object_type.ptr()->AddField(
      Identifier(L"tree"),
      vm::NewCallback(pool, kPurityTypeReader, [](gc::Ptr<OpenBuffer> buffer) {
        return buffer->parse_tree();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"ApplyTransformation"),
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](gc::Ptr<OpenBuffer> buffer,
             NonNull<std::shared_ptr<editor::transformation::Variant>>
                 transformation) {
            return buffer->ApplyToCursors(transformation.value());
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"PushTransformationStack"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer->PushTransformationStack();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"PopTransformationStack"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer->PopTransformationStack();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"AddKeyboardTextTransformer"),
      vm::Value::NewFunction(
          pool, kPurityTypeUnknown, vm::types::Bool{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm::types::String{}},
                               .inputs = {vm::types::String{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            auto buffer = vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::get(
                args[0].ptr().value());
            return vm::Value::NewBool(
                pool, buffer->AddKeyboardTextTransformer(std::move(args[1])));
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"Filter"),
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
      Identifier(L"Reload"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        buffer->Reload();
        buffer->editor().ResetModifiers();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"SendEndOfFileToProcess"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        buffer->SendEndOfFileToProcess();
        buffer->editor().ResetModifiers();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"Save"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        buffer = MaybeFollowOutgoingLink(std::move(buffer));
        futures::Value<PossibleError> output =
            buffer->Save(OpenBuffer::Options::SaveType::kMainFile);
        buffer->editor().ResetModifiers();
        return output;
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"Close"),
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [](gc::Ptr<OpenBuffer> buffer) {
                        buffer = MaybeFollowOutgoingLink(std::move(buffer));
                        buffer->editor().CloseBuffer(buffer.value());
                        buffer->editor().ResetModifiers();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"AddBinding"),
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
                args[2].ptr()->get_string(), std::move(args[3]),
                buffer->environment());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  // TODO(easy, 2024-05-29): When capturing `buffer`, maybe capture a weakptr
  // or ensure that we expand it somehow. Otherwise, it may get collected under
  // our feet. Probably can't happen in practice, but it would be good to use
  // the type system to ensure that.
  buffer_object_type.ptr()->AddField(
      Identifier(L"AddBindingToFile"),
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
                    std::wstring resolved_path;
                    ResolvePathOptions<EmptyValue>::New(
                        buffer->editor(), MakeNonNullShared<FileSystemDriver>(
                                              buffer->editor().thread_pool()))
                        .Transform([buffer, path](
                                       ResolvePathOptions<EmptyValue> options) {
                          // TODO(easy, 2024-01-05): Avoid call to ToString.
                          options.path = path.ToString();
                          return futures::OnError(
                              ResolvePath(std::move(options))
                                  .Transform([buffer, path](
                                                 ResolvePathOutput<EmptyValue>
                                                     results) {
                                    buffer->EvaluateFile(results.path);
                                    return Success();
                                  }),
                              [buffer, path](Error error) {
                                buffer->status().Set(
                                    // TODO(2024-01-05): Avoid call to ToString.
                                    AugmentError(
                                        (LazyString{L"Unable to resolve: "} +
                                         path)
                                            .ToString(),
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
      Identifier(L"ShowTrackers"),
      vm::NewCallback(
          pool, PurityType{.writes_external_outputs = true},
          [](gc::Ptr<OpenBuffer> buffer) {
            buffer->AppendLines(container::MaterializeVector(
                Tracker::GetData() |
                std::views::transform([](Tracker::Data data) -> const Line {
                  return LineBuilder(
                             LazyString{L"\""} + LazyString{data.name} +
                             LazyString{L"\","} +
                             LazyString{std::to_wstring(data.executions)} +
                             LazyString{L","} +
                             LazyString{std::to_wstring(data.seconds)} +
                             LazyString{L","} +
                             LazyString{std::to_wstring(data.longest_seconds)})
                      .Build();
                })));
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"EvaluateFile"),
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [](gc::Ptr<OpenBuffer> buffer, Path path) {
                        buffer->EvaluateFile(std::move(path));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"WaitForEndOfFile"),
      vm::NewCallback(pool, kPurityTypeUnknown, [](gc::Ptr<OpenBuffer> buffer) {
        return buffer->WaitForEndOfFile();
      }).ptr());

  buffer_object_type.ptr()->AddField(
      Identifier(L"LineMetadataString"),
      vm::NewCallback(pool, kPurityTypeReader,
                      [](gc::Ptr<OpenBuffer> buffer, int line_number) {
                        return ToFuture(buffer->contents()
                                            .at(LineNumber(line_number))
                                            .metadata_future())
                            .Transform([](LazyString str_value) {
                              return Success(str_value.ToString());
                            });
                      })
          .ptr());
  environment.DefineType(buffer_object_type.ptr());
  vm::container::Export<std::vector<gc::Ptr<OpenBuffer>>>(pool, environment);
}
}  // namespace afc::editor
