#include "src/buffer_vm.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/tracker.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/line_column_vm.h"
#include "src/vm_transformation.h"

namespace afc::vm {
using language::NonNull;
namespace gc = language::gc;

struct BufferWrapper {
  gc::Ptr<editor::OpenBuffer> buffer;
};

gc::Root<editor::OpenBuffer> VMTypeMapper<gc::Root<editor::OpenBuffer>>::get(
    Value& value) {
  auto wrapper =
      static_cast<BufferWrapper*>(value.get_user_value(vmtype).get());
  CHECK(wrapper != nullptr);
  return wrapper->buffer.ToRoot();
}

// TODO(easy, 2022-05-16): Receive the value as Ptr?
/* static */ gc::Root<Value> VMTypeMapper<gc::Root<editor::OpenBuffer>>::New(
    gc::Pool& pool, gc::Root<editor::OpenBuffer> value) {
  auto wrapper =
      std::make_shared<BufferWrapper>(BufferWrapper{.buffer = value.ptr()});
  return Value::NewObject(
      pool, VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype.object_type,
      std::shared_ptr<void>(wrapper, wrapper.get()),
      [control_frame = wrapper->buffer.control_frame()] {
        return std::vector<NonNull<std::shared_ptr<gc::ControlFrame>>>(
            {control_frame});
      });
}

const VMType VMTypeMapper<gc::Root<editor::OpenBuffer>>::vmtype =
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
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;

namespace gc = language::gc;
namespace {
template <typename EdgeStruct, typename FieldValue>
void RegisterBufferFields(
    gc::Pool& pool, EdgeStruct* edge_struct, afc::vm::ObjectType& object_type,
    const FieldValue& (OpenBuffer::*reader)(const EdgeVariable<FieldValue>*)
        const,
    void (OpenBuffer::*setter)(const EdgeVariable<FieldValue>*, FieldValue)) {
  VMType buffer_type = object_type.type();

  vector<wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    // Getter.
    object_type.AddField(
        variable->name(),
        vm::NewCallback(pool, PurityTypeReader,
                        [reader, variable](gc::Root<OpenBuffer> buffer) {
                          DVLOG(4) << "Buffer field reader is returning.";
                          return (buffer.ptr().value().*reader)(variable);
                        }));

    // Setter.
    object_type.AddField(
        L"set_" + variable->name(),
        vm::NewCallback(
            pool, PurityType::kUnknown,
            [variable, setter](gc::Root<OpenBuffer> buffer, FieldValue value) {
              (buffer.ptr().value().*setter)(variable, value);
            }));
  }
}
}  // namespace

NonNull<std::unique_ptr<ObjectType>> BuildBufferType(gc::Pool& pool) {
  auto buffer =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<gc::Root<OpenBuffer>>::vmtype);

  RegisterBufferFields<EdgeStruct<bool>, bool>(
      pool, buffer_variables::BoolStruct(), buffer.value(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<wstring>, wstring>(
      pool, buffer_variables::StringStruct(), buffer.value(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<int>, int>(
      pool, buffer_variables::IntStruct(), buffer.value(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<double>, double>(
      pool, buffer_variables::DoubleStruct(), buffer.value(), &OpenBuffer::Read,
      &OpenBuffer::Set);
  RegisterBufferFields<EdgeStruct<LineColumn>, LineColumn>(
      pool, buffer_variables::LineColumnStruct(), buffer.value(),
      &OpenBuffer::Read, &OpenBuffer::Set);

  buffer->AddField(
      L"SetStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, wstring s) {
                        buffer.ptr()->status().SetInformationText(s);
                      }));

  buffer->AddField(
      L"SetWarningStatus",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, std::wstring s) {
                        buffer.ptr()->status().SetWarningText(s);
                      }));

  buffer->AddField(
      L"line_count",
      vm::NewCallback(pool, PurityTypeReader, [](gc::Root<OpenBuffer> buffer) {
        return static_cast<int>(buffer.ptr()->contents().size().line_delta);
      }));

  buffer->AddField(
      L"set_position",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](gc::Root<OpenBuffer> buffer, LineColumn position) {
                        buffer.ptr()->set_position(position);
                      }));

  buffer->AddField(
      L"position",
      vm::NewCallback(pool, PurityTypeReader, [](gc::Root<OpenBuffer> buffer) {
        return LineColumn(buffer.ptr()->position());
      }));

  buffer->AddField(
      L"line",
      vm::NewCallback(pool, PurityTypeReader,
                      [](gc::Root<OpenBuffer> buffer, int line_input) {
                        LineNumber line = std::min(
                            LineNumber(std::max(line_input, 0)),
                            LineNumber(0) + buffer.ptr()->lines_size() -
                                LineNumberDelta(1));
                        return buffer.ptr()->contents().at(line)->ToString();
                      }));

  buffer->AddField(
      L"ApplyTransformation",
      Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Void(), buffer->type(),
           vm::VMTypeMapper<editor::transformation::Variant*>::vmtype},
          [&pool](std::vector<gc::Root<Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 2ul);
            auto buffer =
                VMTypeMapper<gc::Root<OpenBuffer>>::get(args[0].ptr().value());
            auto transformation =
                VMTypeMapper<editor::transformation::Variant*>::get(
                    args[1].ptr().value());
            return buffer.ptr()
                ->ApplyToCursors(Pointer(transformation).Reference())
                .Transform([&pool](EmptyValue) {
                  return EvaluationOutput::Return(Value::NewVoid(pool));
                });
          }));

#if 0
  buffer->AddField(
      L"GetRegion",
      Value::NewFunction(
          {VMType::ObjectType(L"Range"), buffer->type(),
           VMType::String()},
          [](vector<gc::Root<Value>> args, Trampoline& trampoline) {
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
                    resume(Value::NewObject(
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
          }));
#endif

  buffer->AddField(L"PushTransformationStack",
                   vm::NewCallback(pool, PurityType::kUnknown,
                                   [](gc::Root<OpenBuffer> buffer) {
                                     buffer.ptr()->PushTransformationStack();
                                   }));

  buffer->AddField(L"PopTransformationStack",
                   vm::NewCallback(pool, PurityType::kUnknown,
                                   [](gc::Root<OpenBuffer> buffer) {
                                     buffer.ptr()->PopTransformationStack();
                                   }));

  buffer->AddField(
      L"AddKeyboardTextTransformer",
      Value::NewFunction(
          pool, PurityType::kUnknown,
          {VMType::Bool(), buffer->type(),
           VMType::Function({VMType::String(), VMType::String()})},
          [&pool](std::vector<gc::Root<Value>> args) {
            CHECK_EQ(args.size(), size_t(2));
            auto buffer =
                VMTypeMapper<gc::Root<OpenBuffer>>::get(args[0].ptr().value());
            return Value::NewBool(
                pool,
                buffer.ptr()->AddKeyboardTextTransformer(std::move(args[1])));
          }));

  buffer->AddField(L"Filter",
                   Value::NewFunction(
                       pool, PurityType::kUnknown,
                       {VMType::Void(), buffer->type(),
                        VMType::Function({VMType::Bool(), VMType::String()})},
                       [&pool](std::vector<gc::Root<Value>> args) {
                         CHECK_EQ(args.size(), size_t(2));
                         auto buffer = VMTypeMapper<gc::Root<OpenBuffer>>::get(
                             args[0].ptr().value());
                         buffer.ptr()->set_filter(std::move(args[1]));
                         return Value::NewVoid(pool);
                       }));

  buffer->AddField(
      L"Reload",
      vm::NewCallback(
          pool, PurityType::kUnknown, [](gc::Root<OpenBuffer> buffer) {
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
          }));

  buffer->AddField(
      L"SendEndOfFileToProcess",
      vm::NewCallback(
          pool, PurityType::kUnknown, [](gc::Root<OpenBuffer> buffer) {
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
          }));

  buffer->AddField(
      L"Save",
      Value::NewFunction(
          pool, PurityType::kUnknown, {VMType::Void(), buffer->type()},
          [&pool](std::vector<gc::Root<Value>> args, Trampoline&) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer =
                VMTypeMapper<gc::Root<OpenBuffer>>::get(args[0].ptr().value());
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
                  if (result.IsError())
                    consumer(result.error());
                  else
                    consumer(EvaluationOutput::Return(Value::NewVoid(pool)));
                });
            buffer.ptr()->editor().ResetModifiers();
            return std::move(future.value);
          }));

  buffer->AddField(
      L"Close",
      vm::NewCallback(pool, PurityTypeWriter, [](gc::Root<OpenBuffer> buffer) {
        if (buffer.ptr()->editor().structure() == StructureLine()) {
          auto target_buffer =
              buffer.ptr()->current_line()->buffer_line_column();
          if (target_buffer.has_value()) {
            VisitPointer(
                target_buffer->buffer.Lock(),
                [&](gc::Root<OpenBuffer> target_root) { buffer = target_root; },
                [] {});
          }
        }
        buffer.ptr()->editor().CloseBuffer(buffer.ptr().value());
        buffer.ptr()->editor().ResetModifiers();
      }));

  buffer->AddField(
      L"AddBinding",
      Value::NewFunction(
          pool, PurityTypeWriter,
          {VMType::Void(), buffer->type(), VMType::String(), VMType::String(),
           VMType::Function({VMType::Void()})},
          [&pool](std::vector<gc::Root<Value>> args) {
            CHECK_EQ(args.size(), 4u);
            gc::Root<OpenBuffer> buffer =
                VMTypeMapper<gc::Root<OpenBuffer>>::get(args[0].ptr().value());
            buffer.ptr()->default_commands()->Add(
                args[1].ptr()->get_string(), args[2].ptr()->get_string(),
                std::move(args[3]), buffer.ptr()->environment().ToRoot());
            return Value::NewVoid(pool);
          }));

  buffer->AddField(
      L"AddBindingToFile",
      vm::NewCallback(
          pool, PurityTypeWriter,
          [](gc::Root<OpenBuffer> buffer, wstring keys, wstring path) {
            LOG(INFO) << "AddBindingToFile: " << keys << " -> " << path;
            buffer.ptr()->default_commands()->Add(
                keys,
                [buffer, path]() {
                  wstring resolved_path;
                  auto options = ResolvePathOptions::New(
                      buffer.ptr()->editor(),
                      std::make_shared<FileSystemDriver>(
                          buffer.ptr()->editor().thread_pool()));
                  options.path = path;
                  futures::OnError(
                      ResolvePath(std::move(options))
                          .Transform([buffer, path](ResolvePathOutput results) {
                            buffer.ptr()->EvaluateFile(results.path);
                            return Success();
                          }),
                      [buffer, path](Error error) {
                        buffer.ptr()->status().SetWarningText(
                            L"Unable to resolve: " + path + L": " +
                            error.description);
                        return futures::Past(Success());
                      });
                },
                L"Load file: " + path);
          }));

  buffer->AddField(
      L"ShowTrackers",
      vm::NewCallback(pool, PurityTypeReader, [](gc::Root<OpenBuffer> buffer) {
        for (auto& data : Tracker::GetData()) {
          buffer.ptr()->AppendLine(StringAppend(
              StringAppend(NewLazyString(data.name), NewLazyString(L": ")),
              NewLazyString(std::to_wstring(data.executions)),
              NewLazyString(L" "),
              NewLazyString(std::to_wstring(data.seconds))));
        }
      }));

  buffer->AddField(
      L"EvaluateFile",
      vm::NewCallback(pool, PurityTypeWriter,
                      [](gc::Root<OpenBuffer> buffer, wstring path_str) {
                        auto path = Path::FromString(path_str);
                        if (path.IsError()) {
                          LOG(ERROR) << path.error().description;
                          return;
                        }
                        buffer.ptr()->EvaluateFile(std::move(path.value()));
                      }));

  buffer->AddField(
      L"WaitForEndOfFile",
      Value::NewFunction(
          pool, PurityType::kUnknown, {VMType::Void(), buffer->type()},
          [](vector<gc::Root<Value>> args, Trampoline& trampoline) {
            CHECK_EQ(args.size(), 1ul);
            auto buffer =
                VMTypeMapper<gc::Root<OpenBuffer>>::get(args[0].ptr().value());
            return buffer.ptr()->WaitForEndOfFile().Transform(
                [&pool = trampoline.pool()](EmptyValue) {
                  return EvaluationOutput::Return(Value::NewVoid(pool));
                });
          }));

  return buffer;
}
}  // namespace afc::editor
