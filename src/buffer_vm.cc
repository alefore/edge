#include "src/buffer_vm.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
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

namespace gc = afc::language::gc;
namespace numbers = afc::math::numbers;

using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::infrastructure::screen::CursorsSet;
using afc::language::EmptyValue;
using afc::language::Error;
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
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::OutgoingLink;
using afc::language::text::Range;
using afc::vm::ObjectType;
using afc::vm::PurityType;
using afc::vm::Trampoline;

namespace afc::vm {
struct BufferWrapper {
  const gc::Ptr<editor::OpenBuffer> buffer;
};

gc::Root<editor::OpenBuffer>
vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::get(Value& value) {
  BufferWrapper wrapper =
      value.get_user_value<BufferWrapper>(object_type_name).value();
  return wrapper.buffer.ToRoot();
}

/* static */ gc::Root<vm::Value>
VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  return vm::Value::NewObject(
      pool, object_type_name,
      MakeNonNullShared<BufferWrapper>(BufferWrapper{.buffer = value.ptr()}),
      [object_metadata = value.ptr().object_metadata()] {
        return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
            {object_metadata});
      });
}

const vm::types::ObjectName
    vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::object_type_name =
        vm::types::ObjectName(L"Buffer");
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

  std::vector<std::wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const std::wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type.ptr()->AddField(
        variable->name(),
        vm::NewCallback(pool, PurityType::kReader,
                        [reader, variable](gc::Root<OpenBuffer> buffer) {
                          DVLOG(4) << "Buffer field reader is returning.";
                          return (buffer.ptr().value().*reader)(variable);
                        })
            .ptr());

    // Setter.
    object_type.ptr()->AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            pool, PurityType::kUnknown,
            [variable, setter](gc::Root<OpenBuffer> buffer, FieldValue value) {
              (buffer.ptr().value().*setter)(variable, value);
            })
            .ptr());
  }
}

gc::Root<OpenBuffer> MaybeFollowOutgoingLink(gc::Root<OpenBuffer> buffer) {
  if (buffer.ptr()->editor().structure() == Structure::kLine) {
    return VisitPointer(
        buffer.ptr()->CurrentLine()->outgoing_link(),
        [&](const OutgoingLink& link) {
          if (auto it =
                  buffer.ptr()->editor().buffers()->find(BufferName(link.path));
              it != buffer.ptr()->editor().buffers()->end())
            return it->second;
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
               ->contents()
               ->size()
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
      L"SortLinesByKey",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm_type_key},
                               .inputs = {vm::types::Number{}}}},
          [get_key](std::vector<gc::Root<vm::Value>> args,
                    Trampoline& trampoline) {
            CHECK_EQ(args.size(), size_t(2));

            struct Data {
              Trampoline& trampoline;
              gc::Root<OpenBuffer> buffer;
              PossibleError possible_error = Success();
              gc::Root<gc::ValueWithFixedDependencies<vm::Value::Callback>>
                  callback;
              std::unordered_map<std::wstring, KeyType> keys = {};
            };

            const auto data = MakeNonNullShared<Data>(
                Data{.trampoline = trampoline,
                     .buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                         args[0].ptr().value()),
                     .callback = args[1].ptr()->LockCallback()});

            const std::pair<LineNumber, LineNumberDelta> boundaries =
                GetBoundariesForTransformation(
                    data->buffer.ptr()->active_cursors(),
                    data->buffer.ptr()->contents().snapshot());

            LOG(INFO) << "Sorting with boundaries: " << boundaries.first << " "
                      << boundaries.second;
            // We build `inputs` simply to be able to use futures::ForEach.
            NonNull<std::shared_ptr<std::vector<LineNumber>>> inputs;
            data->buffer.ptr()->contents().snapshot().ForEachLine(
                boundaries.first, boundaries.second,
                [&inputs](LineNumber number,
                          const NonNull<std::shared_ptr<const Line>>&) {
                  inputs->push_back(number);
                  return true;
                });

            return futures::ForEach(
                       inputs.get_shared(),
                       [data, get_key](LineNumber line_number) {
                         return data->callback.ptr()
                             ->value(
                                 {vm::Value::NewNumber(
                                     data->trampoline.pool(),
                                     numbers::FromSizeT(line_number.read()))},
                                 data->trampoline)
                             .Transform([data, get_key, line_number](
                                            gc::Root<vm::Value> output)
                                            -> ValueOrError<
                                                futures::
                                                    IterationControlCommand> {
                               auto line = data->buffer.ptr()->contents().at(
                                   line_number);
                               VLOG(9) << "Value for line: " << line.value()
                                       << ": " << get_key(output.ptr().value());
                               ASSIGN_OR_RETURN(auto key_value,
                                                get_key(output.ptr().value()));
                               data->keys.insert({line->ToString(), key_value});
                               return Success(
                                   futures::IterationControlCommand::kContinue);
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
                            data->buffer.ptr()->SortContents(
                                boundaries.first, boundaries.second,
                                [data](const language::NonNull<std::shared_ptr<
                                           const language::text::Line>>& a,
                                       const language::NonNull<std::shared_ptr<
                                           const language::text::Line>>& b) {
                                  auto it_a = data->keys.find(
                                      a->contents()->ToString());
                                  auto it_b = data->keys.find(
                                      b->contents()->ToString());
                                  CHECK(it_a != data->keys.end());
                                  CHECK(it_b != data->keys.end());
                                  VLOG(10) << "Sort key: "
                                           << a->contents()->ToString() << ": "
                                           << it_a->second;
                                  VLOG(10) << "Sort key: "
                                           << b->contents()->ToString() << ": "
                                           << it_b->second;
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

gc::Root<ObjectType> BuildBufferType(gc::Pool& pool) {
  gc::Root<ObjectType> buffer_object_type = ObjectType::New(
      pool, vm::VMTypeMapper<gc::Root<OpenBuffer>>::object_type_name);

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
      L"SetStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, std::wstring s) {
                        buffer.ptr()->status().SetInformationText(
                            MakeNonNullShared<Line>(s));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"SetWarningStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, std::wstring s) {
                        buffer.ptr()->status().InsertError(Error(s));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"child_exit_status",
      vm::NewCallback(pool, PurityType::kReader,
                      [](gc::Root<OpenBuffer> buffer) {
                        return static_cast<int>(
                            buffer.ptr()->child_exit_status().value_or(0));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"tostring", vm::NewCallback(pool, PurityType::kReader,
                                   [](gc::Root<OpenBuffer> buffer) {
                                     return buffer.ptr()->name().read();
                                   })
                       .ptr());

  buffer_object_type.ptr()->AddField(
      L"line_count",
      vm::NewCallback(pool, PurityType::kReader,
                      [](gc::Root<OpenBuffer> buffer) {
                        return static_cast<int>(
                            buffer.ptr()->contents().size().read());
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"set_position",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, LineColumn position) {
                        buffer.ptr()->set_position(position);
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"position",
      vm::NewCallback(pool, PurityType::kReader,
                      [](gc::Root<OpenBuffer> buffer) {
                        return LineColumn(buffer.ptr()->position());
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"active_cursors",
      vm::NewCallback(pool, PurityType::kReader,
                      [](gc::Root<OpenBuffer> buffer) {
                        const CursorsSet& cursors =
                            buffer.ptr()->active_cursors();
                        return MakeNonNullShared<std::vector<LineColumn>>(
                            cursors.begin(), cursors.end());
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"set_active_cursors",
      vm::NewCallback(
          pool, PurityType::kReader,
          [](gc::Root<OpenBuffer> buffer,
             NonNull<std::shared_ptr<std::vector<LineColumn>>> cursors) {
            buffer.ptr()->set_active_cursors(cursors.value());
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"line",
      vm::NewCallback(pool, PurityType::kReader,
                      [](gc::Root<OpenBuffer> buffer, int line_input) {
                        LineNumber line = std::min(
                            LineNumber(std::max(line_input, 0)),
                            LineNumber(0) + buffer.ptr()->lines_size() -
                                LineNumberDelta(1));
                        return buffer.ptr()->contents().at(line)->ToString();
                      })
          .ptr());

  DefineSortLinesByKey<int64_t>(pool, buffer_object_type, vm::types::Number{},
                                [](const vm::Value& value) {
                                  return numbers::ToInt(value.get_number());
                                });

  DefineSortLinesByKey<std::wstring>(
      pool, buffer_object_type, vm::types::String{},
      [](const vm::Value& value) { return Success(value.get_string()); });

  buffer_object_type.ptr()->AddField(
      L"tree", vm::NewCallback(pool, PurityType::kReader,
                               [](gc::Root<OpenBuffer> buffer) {
                                 return buffer.ptr()->parse_tree();
                               })
                   .ptr());

  buffer_object_type.ptr()->AddField(
      L"ApplyTransformation",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](gc::Root<OpenBuffer> buffer,
             NonNull<std::shared_ptr<editor::transformation::Variant>>
                 transformation) {
            return buffer.ptr()->ApplyToCursors(transformation.value());
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"PushTransformationStack",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer) {
                        buffer.ptr()->PushTransformationStack();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"PopTransformationStack",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer) {
                        buffer.ptr()->PopTransformationStack();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"AddKeyboardTextTransformer",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Bool{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm::types::String{}},
                               .inputs = {vm::types::String{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            return vm::Value::NewBool(
                pool,
                buffer.ptr()->AddKeyboardTextTransformer(std::move(args[1])));
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"Filter",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm::types::String{}},
                               .inputs = {vm::types::String{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            buffer.ptr()->set_filter(std::move(args[1]));
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"Reload",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer) {
                        buffer = MaybeFollowOutgoingLink(std::move(buffer));
                        buffer.ptr()->Reload();
                        buffer.ptr()->editor().ResetModifiers();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"SendEndOfFileToProcess",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer) {
                        buffer = MaybeFollowOutgoingLink(std::move(buffer));
                        buffer.ptr()->SendEndOfFileToProcess();
                        buffer.ptr()->editor().ResetModifiers();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"Save", vm::NewCallback(
                   pool, PurityType::kUnknown,
                   [](gc::Root<OpenBuffer> buffer) {
                     buffer = MaybeFollowOutgoingLink(std::move(buffer));
                     futures::Value<PossibleError> output = buffer.ptr()->Save(
                         OpenBuffer::Options::SaveType::kMainFile);
                     buffer.ptr()->editor().ResetModifiers();
                     return output;
                   })
                   .ptr());

  buffer_object_type.ptr()->AddField(
      L"Close", vm::NewCallback(
                    pool, vm::PurityTypeWriter,
                    [](gc::Root<OpenBuffer> buffer) {
                      buffer = MaybeFollowOutgoingLink(std::move(buffer));
                      buffer.ptr()->editor().CloseBuffer(buffer.ptr().value());
                      buffer.ptr()->editor().ResetModifiers();
                    })
                    .ptr());

  buffer_object_type.ptr()->AddField(
      L"AddBinding",
      vm::Value::NewFunction(
          pool, vm::PurityTypeWriter, vm::types::Void{},
          {buffer_object_type.ptr()->type(), vm::types::String{},
           vm::types::String{},
           vm::types::Function{.output = vm::Type{vm::types::Void{}}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 4u);
            gc::Root<OpenBuffer> buffer =
                vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                    args[0].ptr().value());
            buffer.ptr()->default_commands().ptr()->Add(
                args[1].ptr()->get_string(), args[2].ptr()->get_string(),
                std::move(args[3]), buffer.ptr()->environment().ToRoot());
            return vm::Value::NewVoid(pool);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"AddBindingToFile",
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](gc::Root<OpenBuffer> buffer, std::wstring keys,
             std::wstring path) {
            LOG(INFO) << "AddBindingToFile: " << keys << " -> " << path;
            buffer.ptr()->default_commands().ptr()->Add(
                keys,
                [buffer, path]() {
                  std::wstring resolved_path;
                  ResolvePathOptions<EmptyValue>::New(
                      buffer.ptr()->editor(),
                      MakeNonNullShared<FileSystemDriver>(
                          buffer.ptr()->editor().thread_pool()))
                      .Transform([buffer, path](
                                     ResolvePathOptions<EmptyValue> options) {
                        options.path = path;
                        return futures::OnError(
                            ResolvePath(std::move(options))
                                .Transform(
                                    [buffer, path](
                                        ResolvePathOutput<EmptyValue> results) {
                                      buffer.ptr()->EvaluateFile(results.path);
                                      return Success();
                                    }),
                            [buffer, path](Error error) {
                              buffer.ptr()->status().Set(
                                  AugmentError(L"Unable to resolve: " + path,
                                               std::move(error)));
                              return futures::Past(Success());
                            });
                      });
                },
                L"Load file: " + path);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"ShowTrackers",
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](gc::Root<OpenBuffer> buffer) {
            for (auto& data : Tracker::GetData()) {
              buffer.ptr()->AppendLine(Append(
                  Append(NewLazyString(L"\""), NewLazyString(data.name),
                         NewLazyString(L"\","),
                         NewLazyString(std::to_wstring(data.executions))),
                  Append(
                      NewLazyString(L","),
                      NewLazyString(std::to_wstring(data.seconds)),
                      NewLazyString(L","),
                      NewLazyString(std::to_wstring(data.longest_seconds)))));
            }
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"EvaluateFile",
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      [](gc::Root<OpenBuffer> buffer, Path path) {
                        buffer.ptr()->EvaluateFile(std::move(path));
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"WaitForEndOfFile",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer) {
                        return buffer.ptr()->WaitForEndOfFile();
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"LineMetadataString",
      vm::NewCallback(
          pool, PurityType::kPure,
          [](gc::Root<OpenBuffer> buffer, int line_number) {
            language::NonNull<std::shared_ptr<const Line>> line =
                buffer.ptr()->contents().at(LineNumber(line_number));
            return ToFuture(line->metadata_future())
                .Transform([](NonNull<std::shared_ptr<LazyString>> str_value) {
                  return Success(str_value->ToString());
                });
          })
          .ptr());
  return buffer_object_type;
}
}  // namespace afc::editor
