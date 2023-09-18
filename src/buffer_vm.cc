#include "src/buffer_vm.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
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

namespace afc::vm {
using language::MakeNonNullShared;
using language::NonNull;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

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
using infrastructure::FileSystemDriver;
using infrastructure::Path;
using infrastructure::Tracker;
using infrastructure::screen::CursorsSet;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::OutgoingLink;
using vm::EvaluationOutput;
using vm::ObjectType;
using vm::PurityType;
using vm::Trampoline;

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

template <typename KeyType>
void DefineSortLinesByKey(gc::Pool& pool,
                          gc::Root<ObjectType>& buffer_object_type,
                          vm::Type vm_type_key,
                          std::function<KeyType(const vm::Value&)> get_key) {
  buffer_object_type.ptr()->AddField(
      L"SortLinesByKey",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown, vm::types::Void{},
          {buffer_object_type.ptr()->type(),
           vm::types::Function{.output = vm::Type{vm_type_key},
                               .inputs = {vm::types::Int{}}}},
          [get_key](std::vector<gc::Root<vm::Value>> args,
                    Trampoline& trampoline) {
            CHECK_EQ(args.size(), size_t(2));
            struct Data {
              Trampoline& trampoline;
              gc::Root<OpenBuffer> buffer;
              PossibleError possible_error = Success();
              vm::Value::Callback callback;
              std::unordered_map<std::wstring, KeyType> keys = {};
            };

            auto data = MakeNonNullShared<Data>(
                Data{.trampoline = trampoline,
                     .buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                         args[0].ptr().value()),
                     .callback = args[1].ptr()->LockCallback()});

            // We build `inputs` simply to be able to use futures::ForEach.
            NonNull<std::shared_ptr<std::vector<LineNumber>>> inputs;
            data->buffer.ptr()->contents().EveryLine(
                [inputs](LineNumber number, const Line&) {
                  inputs->push_back(number);
                  return true;
                });

            return futures::ForEach(
                       inputs.get_shared(),
                       [data, get_key](LineNumber line_number) {
                         return data
                             ->callback(
                                 {vm::Value::NewInt(data->trampoline.pool(),
                                                    line_number.read())},
                                 data->trampoline)
                             .Transform([data, get_key,
                                         line_number](EvaluationOutput output) {
                               data->keys.insert(
                                   {data->buffer.ptr()
                                        ->contents()
                                        .at(line_number)
                                        ->ToString(),
                                    get_key(output.value.ptr().value())});
                               return futures::Past(
                                   Success(futures::IterationControlCommand::
                                               kContinue));
                             })
                             .ConsumeErrors([data](Error error_input) {
                               data->possible_error = error_input;
                               return futures::Past(
                                   futures::IterationControlCommand::kStop);
                             });
                       })
                .Transform([data](futures::IterationControlCommand)
                               -> ValueOrError<EvaluationOutput> {
                  return std::visit(
                      overload{
                          [](Error error) {
                            return ValueOrError<EvaluationOutput>(error);
                          },
                          [data](EmptyValue) {
                            data->buffer.ptr()->SortAllContents(
                                [data](const language::NonNull<std::shared_ptr<
                                           const language::text::Line>>& a,
                                       const language::NonNull<std::shared_ptr<
                                           const language::text::Line>>& b) {
                                  return data->keys
                                             .find(a->contents()->ToString())
                                             ->second <
                                         data->keys
                                             .find(b->contents()->ToString())
                                             ->second;
                                });
                            return Success(
                                EvaluationOutput{.value = vm::Value::NewVoid(
                                                     data->trampoline.pool())});
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

  DefineSortLinesByKey<int>(
      pool, buffer_object_type, vm::types::Int{},
      [](const vm::Value& value) { return value.get_int(); });

  // TODO(2023-09-16): Very interestingly, this isn't showing up. There must be
  // something lacking in the polymorphism support, which is very sad.
  DefineSortLinesByKey<std::wstring>(
      pool, buffer_object_type, vm::types::String{},
      [](const vm::Value& value) { return value.get_string(); });

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
      L"Save", vm::NewCallback(pool, PurityType::kUnknown,
                               [](gc::Root<OpenBuffer> buffer) {
                                 buffer =
                                     MaybeFollowOutgoingLink(std::move(buffer));
                                 futures::Value<PossibleError> output =
                                     buffer.ptr()->Save();
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
            buffer.ptr()->default_commands()->Add(
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
            buffer.ptr()->default_commands()->Add(
                keys,
                [buffer, path]() {
                  std::wstring resolved_path;
                  ResolvePathOptions<EmptyValue>::New(
                      buffer.ptr()->editor(),
                      std::make_shared<FileSystemDriver>(
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
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](gc::Root<OpenBuffer> buffer, std::wstring path_str) {
            std::visit(overload{[](Error error) { LOG(ERROR) << error; },
                                [&](Path path) {
                                  buffer.ptr()->EvaluateFile(std::move(path));
                                }},
                       Path::FromString(path_str));
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
