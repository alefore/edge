#include "src/buffer_vm.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/line_column_vm.h"
#include "src/transformation/vm.h"

namespace afc::vm {
using language::MakeNonNullShared;
using language::NonNull;
namespace gc = language::gc;

struct BufferWrapper {
  const gc::Ptr<editor::OpenBuffer> buffer;
};

gc::Root<editor::OpenBuffer>
vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::get(Value& value) {
  BufferWrapper wrapper = value.get_user_value<BufferWrapper>(vmtype).value();
  return wrapper.buffer.ToRoot();
}

/* static */ gc::Root<vm::Value>
VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  return vm::Value::NewObject(
      pool, vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype.object_type,
      MakeNonNullShared<BufferWrapper>(BufferWrapper{.buffer = value.ptr()}),
      [object_metadata = value.ptr().object_metadata()] {
        return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
            {object_metadata});
      });
}

const VMType vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Buffer"));
}  // namespace afc::vm

namespace afc::editor {
using infrastructure::FileSystemDriver;
using infrastructure::Path;
using infrastructure::Tracker;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using vm::EvaluationOutput;
using vm::ObjectType;
using vm::PurityType;
using vm::Trampoline;
using vm::VMType;

namespace gc = language::gc;
namespace {
template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    gc::Pool& pool, EdgeStruct* edge_struct, gc::Root<ObjectType>& object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  VMType buffer_type = object_type.ptr()->type();

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
}  // namespace

gc::Root<ObjectType> BuildBufferType(gc::Pool& pool) {
  gc::Root<ObjectType> buffer_object_type =
      ObjectType::New(pool, vm::VMTypeMapper<gc::Root<OpenBuffer>>::vmtype);

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
  RegisterBufferFields<EdgeStruct<LineColumn>, LineColumn>(
      pool, buffer_variables::LineColumnStruct(), buffer_object_type,
      &OpenBuffer::Read, &OpenBuffer::Set);

  buffer_object_type.ptr()->AddField(
      L"SetStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, std::wstring s) {
                        buffer.ptr()->status().SetInformationText(s);
                      })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"SetWarningStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, std::wstring s) {
                        buffer.ptr()->status().SetWarningText(s);
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

  buffer_object_type.ptr()->AddField(
      L"ApplyTransformation",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), buffer_object_type.ptr()->type(),
           vm::VMTypeMapper<NonNull<
               std::shared_ptr<editor::transformation::Variant>>>::vmtype},
          [&pool](std::vector<gc::Root<vm::Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 2ul);
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            NonNull<std::shared_ptr<editor::transformation::Variant>>
                transformation = vm::VMTypeMapper<
                    NonNull<std::shared_ptr<editor::transformation::Variant>>>::
                    get(args[1].ptr().value());
            return buffer.ptr()
                ->ApplyToCursors(transformation.value())
                .Transform([&pool](EmptyValue) {
                  return EvaluationOutput::Return(vm::Value::NewVoid(pool));
                });
          })
          .ptr());

#if 0
  buffer_object_type.ptr()->AddField(
      L"GetRegion",
      vm::Value::NewFunction(
          {VMType::ObjectType(L"Range"), buffer_object_type.ptr()->type(),
           VMType::String()},
          [](vector<gc::Root<vm::Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 2u);
            CHECK_EQ(args[0]->type, VMType::ObjectType(L"Buffer"));
            CHECK_EQ(args[1]->type, VMType::VM_STRING);
            // TODO: Don't ignore the buffer! Apply it to it!
            // auto buffer =
            // static_cast<OpenBuffer*>(args[0]->user_value.get());
            auto resume = trampoline.Interrupt();
            NewCommandWithModifiers(
                args[1]->str, L"Selects a region",
                [resume](EditorState*, OpenBuffer* buffer,
                         CommandApplyMode mode, Modifiers modifiers) {
                  // TODO: Apply this to all cursors. That's tricky, because we
                  // don't know what effect each transformation will have, and
                  // because we can't call `resume` more than once (it will
                  // likely free things when we call it).
                  if (mode == CommandApplyMode::FINAL) {
                    LOG(INFO) << "GetRegion: Resuming.";
                    resume(vm::Value::NewObject(
                        L"Range", std::make_shared<Range>(
                             buffer->FindPartialRange(
                                 modifiers, buffer->position()))));
                  } else {
                    buffer->PushTransformationStack();
                    DeleteOptions options;
                    options.modifiers = modifiers;
                    options.copy_to_paste_buffer = false;
                    buffer->ApplyToCursors(
                        NewDeleteTransformation(options),
                        Modifiers::AFFECT_ONLY_CURRENT_CURSOR,
                        transformation::Input::Mode::kPreview,
                        [buffer] () {
                      buffer->PopTransformationStack();
                    });
                  }
                })
                ->ProcessInput(L'\n', editor_state);
          }).ptr());
#endif

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
          pool, PurityType::kUnknown,
          {VMType::Bool(), buffer_object_type.ptr()->type(),
           VMType::Function({VMType::String(), VMType::String()})},
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
      L"Filter", vm::Value::NewFunction(
                     pool, PurityType::kUnknown,
                     {VMType::Void(), buffer_object_type.ptr()->type(),
                      VMType::Function({VMType::Bool(), VMType::String()})},
                     [&pool](std::vector<gc::Root<vm::Value>> args) {
                       CHECK_EQ(args.size(), size_t(2));
                       auto buffer =
                           vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                               args[0].ptr().value());
                       buffer.ptr()->set_filter(std::move(args[1]));
                       return vm::Value::NewVoid(pool);
                     })
                     .ptr());

  buffer_object_type.ptr()->AddField(
      L"Reload",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](gc::Root<OpenBuffer> buffer) {
            if (buffer.ptr()->editor().structure() == StructureLine()) {
              auto target_buffer =
                  buffer.ptr()->current_line()->buffer_line_column();
              if (target_buffer.has_value()) {
                VisitPointer(
                    target_buffer->buffer.Lock(),
                    [&](gc::Root<OpenBuffer> target_root) {
                      buffer = target_root;
                    },
                    [] {});
              }
            }
            buffer.ptr()->Reload();
            buffer.ptr()->editor().ResetModifiers();
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"SendEndOfFileToProcess",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](gc::Root<OpenBuffer> buffer) {
            if (buffer.ptr()->editor().structure() == StructureLine()) {
              auto target_buffer =
                  buffer.ptr()->current_line()->buffer_line_column();
              if (target_buffer.has_value()) {
                VisitPointer(
                    target_buffer->buffer.Lock(),
                    [&](gc::Root<OpenBuffer> target_root) {
                      buffer = target_root;
                    },
                    [] {});
              }
            }
            buffer.ptr()->SendEndOfFileToProcess();
            buffer.ptr()->editor().ResetModifiers();
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"Save",
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), buffer_object_type.ptr()->type()},
          [&pool](std::vector<gc::Root<vm::Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            if (buffer.ptr()->editor().structure() == StructureLine()) {
              auto target_buffer =
                  buffer.ptr()->current_line()->buffer_line_column();
              if (target_buffer.has_value()) {
                VisitPointer(
                    target_buffer->buffer.Lock(),
                    [&](gc::Root<OpenBuffer> target_root) {
                      buffer = target_root;
                    },
                    [] {});
              }
            }

            futures::Future<ValueOrError<EvaluationOutput>> future;
            buffer.ptr()->Save().SetConsumer(
                [&pool,
                 consumer = std::move(future.consumer)](PossibleError result) {
                  consumer(std::visit(
                      overload{[](Error error) {
                                 return ValueOrError<EvaluationOutput>(error);
                               },
                               [&pool](EmptyValue) {
                                 return Success(EvaluationOutput::Return(
                                     vm::Value::NewVoid(pool)));
                               }},
                      result));
                });
            buffer.ptr()->editor().ResetModifiers();
            return std::move(future.value);
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"Close",
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](gc::Root<OpenBuffer> buffer) {
            if (buffer.ptr()->editor().structure() == StructureLine()) {
              auto target_buffer =
                  buffer.ptr()->current_line()->buffer_line_column();
              if (target_buffer.has_value()) {
                VisitPointer(
                    target_buffer->buffer.Lock(),
                    [&](gc::Root<OpenBuffer> target_root) {
                      buffer = target_root;
                    },
                    [] {});
              }
            }
            buffer.ptr()->editor().CloseBuffer(buffer.ptr().value());
            buffer.ptr()->editor().ResetModifiers();
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"AddBinding",
      vm::Value::NewFunction(
          pool, vm::PurityTypeWriter,
          {VMType::Void(), buffer_object_type.ptr()->type(), VMType::String(),
           VMType::String(), VMType::Function({VMType::Void()})},
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
              buffer.ptr()->AppendLine(
                  Append(Append(NewLazyString(data.name), NewLazyString(L": "),
                                NewLazyString(std::to_wstring(data.executions)),
                                NewLazyString(L" ")),
                         NewLazyString(std::to_wstring(data.seconds)),
                         NewLazyString(L" "),
                         NewLazyString(std::to_wstring(data.longest_seconds))));
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
      vm::Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), buffer_object_type.ptr()->type()},
          [](std::vector<gc::Root<vm::Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            return buffer.ptr()->WaitForEndOfFile().Transform(
                [&pool = trampoline.pool()](EmptyValue) {
                  return EvaluationOutput::Return(vm::Value::NewVoid(pool));
                });
          })
          .ptr());

  buffer_object_type.ptr()->AddField(
      L"LineMetadataString",
      vm::Value::NewFunction(
          pool, PurityType::kPure,
          {VMType::String(), buffer_object_type.ptr()->type(), VMType::Int()},
          [&pool](std::vector<gc::Root<vm::Value>> args,
                  Trampoline&) -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto buffer = vm::VMTypeMapper<gc::Root<OpenBuffer>>::get(
                args[0].ptr().value());
            language::NonNull<std::shared_ptr<const Line>> line =
                buffer.ptr()->contents().at(
                    LineNumber(args[1].ptr()->get_int()));
            return std::visit(
                overload{
                    [](Error error) -> futures::ValueOrError<EvaluationOutput> {
                      return futures::Past(
                          ValueOrError<EvaluationOutput>(std::move(error)));
                    },
                    [&pool](
                        futures::ListenableValue<
                            NonNull<std::shared_ptr<LazyString>>>
                            value) -> futures::ValueOrError<EvaluationOutput> {
                      return value.ToFuture().Transform(
                          [&pool](
                              NonNull<std::shared_ptr<LazyString>> str_value) {
                            return futures::Past(Success(
                                EvaluationOutput::Return(vm::Value::NewString(
                                    pool, str_value->ToString()))));
                          });
                    }},
                line->metadata_future());
          })
          .ptr());
  return buffer_object_type;
}
}  // namespace afc::editor
