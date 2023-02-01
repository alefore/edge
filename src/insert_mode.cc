#include "src/insert_mode.h"

#include <algorithm>
#include <memory>
#include <vector>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/command.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/futures/futures.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/wstring.h"
#include "src/parse_tree.h"
#include "src/terminal.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/expand.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace afc::editor {
using futures::DeleteNotification;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;

using vm::VMType;
using vm::VMTypeMapper;

namespace gc = language::gc;

namespace {
class NewLineTransformation : public CompositeTransformation {
  std::wstring Serialize() const override { return L"NewLineTransformation()"; }
  futures::Value<Output> Apply(Input input) const override {
    const ColumnNumber column = input.position.column;
    auto line = input.buffer.LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    if (input.buffer.Read(buffer_variables::atomic_lines) &&
        column != ColumnNumber(0) && column != line->EndColumn())
      return futures::Past(Output());
    const std::wstring& line_prefix_characters(
        input.buffer.Read(buffer_variables::line_prefix_characters));
    ColumnNumber prefix_end;
    if (!input.buffer.Read(buffer_variables::paste_mode)) {
      while (prefix_end < column &&
             (line_prefix_characters.find(line->get(prefix_end)) !=
              line_prefix_characters.npos)) {
        ++prefix_end;
      }
    }

    Output output;
    {
      NonNull<std::shared_ptr<BufferContents>> contents_to_insert;
      contents_to_insert->push_back(MakeNonNullShared<Line>(
          line->CopyOptions().DeleteSuffix(prefix_end)));
      output.Push(transformation::Insert{.contents_to_insert =
                                             std::move(contents_to_insert)});
    }

    output.Push(transformation::SetPosition(input.position));
    output.Push(NewDeleteSuffixSuperfluousCharacters());
    output.Push(transformation::SetPosition(
        LineColumn(input.position.line + LineNumberDelta(1), prefix_end)));
    return futures::Past(std::move(output));
  }
};

class InsertEmptyLineTransformation : public CompositeTransformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}
  std::wstring Serialize() const override { return L""; }
  futures::Value<Output> Apply(Input input) const override {
    if (direction_ == Direction::kBackwards) {
      ++input.position.line;
    }
    Output output = Output::SetPosition(LineColumn(input.position.line));
    output.Push(transformation::ModifiersAndComposite{
        Modifiers(), MakeNonNullUnique<NewLineTransformation>()});
    output.Push(transformation::SetPosition(input.position));
    return futures::Past(std::move(output));
  }

 private:
  Direction direction_;
};

class FindCompletionCommand : public Command {
 public:
  FindCompletionCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"Autocompletes the current word.";
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) override {
    // TODO(multiple_buffers): Honor.
    VisitPointer(
        editor_state_.current_buffer(),
        [](gc::Root<OpenBuffer> buffer) {
          buffer.ptr()->ApplyToCursors(NewExpandTransformation());
        },
        [] {});
  }

 private:
  EditorState& editor_state_;
};

std::unique_ptr<BufferContents, std::function<void(BufferContents*)>>
NewInsertion(EditorState& editor) {
  return std::unique_ptr<BufferContents, std::function<void(BufferContents*)>>(
      new BufferContents(), [&editor](BufferContents* value) {
        CHECK(value != nullptr);
        editor.insert_history().Append(*value);
        delete value;
      });
}

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options)
      : options_(std::move(options)),
        buffers_(MakeNonNullShared<std::vector<gc::Root<OpenBuffer>>>(
            options_.buffers->begin(), options_.buffers->end())),
        current_insertion_(NewInsertion(options_.editor_state)) {
    CHECK(options_.escape_handler);
    CHECK(options_.buffers.has_value());
    CHECK(!options_.buffers.value().empty());
  }

  void ProcessInput(wint_t c) override {
    bool old_literal = status_expiration_for_literal_ != nullptr;
    status_expiration_for_literal_ = nullptr;

    CHECK(options_.buffers.has_value());
    auto future = futures::Past(futures::IterationControlCommand::kContinue);
    switch (static_cast<int>(c)) {
      case '\t':
        ResetScrollBehavior();
        if (old_literal) {
          LOG(INFO) << "Inserting escaped TAB.";
          break;
        }
        ForEachActiveBuffer(buffers_, "",
                            [options = options_](OpenBuffer& buffer) {
                              // TODO: Don't ignore the return value. If it's
                              // false for all buffers, insert the \t.
                              options.start_completion(buffer);
                              return futures::Past(EmptyValue());
                            });

        // Whatever was being typed, was probably just for completion purposes;
        // we might as well not let it be added to the history (so as to not
        // pollute it).
        current_insertion_->FilterToRange(Range());
        return;

      case Terminal::ESCAPE:
        ResetScrollBehavior();
        StartNewInsertion();

        ForEachActiveBuffer(
            buffers_, old_literal ? std::string{27} : "",
            [options = options_, old_literal](OpenBuffer& buffer) {
              if (buffer.fd() != nullptr) {
                if (old_literal) {
                  buffer.status().SetInformationText(L"ESC");
                } else {
                  buffer.status().Reset();
                }
                return futures::Past(EmptyValue());
              }
              buffer.MaybeAdjustPositionCol();
              gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
              // TODO(easy): Honor `old_literal`.
              return buffer
                  .ApplyToCursors(NewDeleteSuffixSuperfluousCharacters())
                  .Transform([options, buffer_root](EmptyValue) {
                    buffer_root.ptr()->PopTransformationStack();
                    auto repetitions =
                        options.editor_state.repetitions().value_or(1);
                    if (repetitions > 0) {
                      options.editor_state.set_repetitions(repetitions - 1);
                    }
                    return buffer_root.ptr()->RepeatLastTransformation();
                  })
                  .Transform([options, buffer_root](EmptyValue) {
                    buffer_root.ptr()->PopTransformationStack();
                    options.editor_state.PushCurrentPosition();
                    buffer_root.ptr()->status().Reset();
                    return EmptyValue();
                  });
            })
            .Transform([options = options_, old_literal](EmptyValue) {
              if (old_literal) return EmptyValue();
              options.editor_state.status().Reset();
              CHECK(options.escape_handler != nullptr);
              options.escape_handler();  // Probably deletes us.
              options.editor_state.ResetRepetitions();
              options.editor_state.ResetInsertionModifier();
              options.editor_state.set_keyboard_redirect(nullptr);
              return EmptyValue();
            });
        return;

      case Terminal::UP_ARROW:
        ApplyScrollBehavior({27, '[', 'A'}, &ScrollBehavior::Up);
        return;

      case Terminal::DOWN_ARROW:
        ApplyScrollBehavior({27, '[', 'B'}, &ScrollBehavior::Down);
        return;

      case Terminal::LEFT_ARROW:
        ApplyScrollBehavior({27, '[', 'D'}, &ScrollBehavior::Left);
        return;

      case Terminal::RIGHT_ARROW:
        ApplyScrollBehavior({27, '[', 'C'}, &ScrollBehavior::Right);
        return;

      case Terminal::CTRL_A:
        ApplyScrollBehavior({1}, &ScrollBehavior::Begin);
        return;

      case Terminal::CTRL_E:
        ApplyScrollBehavior({5}, &ScrollBehavior::End);
        return;

      case Terminal::CTRL_L:
        WriteLineBuffer(buffers_, {0x0c});
        return;

      case Terminal::CTRL_D:
        HandleDelete({4}, Direction::kForwards);
        return;

      case Terminal::DELETE:
        HandleDelete({27, '[', 51, 126}, Direction::kForwards);
        return;

      case Terminal::BACKSPACE:
        HandleDelete({127}, Direction::kBackwards);
        return;

      case '\n':
        ResetScrollBehavior();

        // TODO(2022-05-22): Not sure StartNewInsertion is the best to do here;
        // would be better to leave a \n in the insertion?
        StartNewInsertion();

        ForEachActiveBuffer(buffers_, {'\n'}, [&](OpenBuffer& buffer) {
          return options_.new_line_handler(buffer);
        });
        return;

      case Terminal::CTRL_U: {
        ResetScrollBehavior();
        StartNewInsertion();
        // TODO: Find a way to set `copy_to_paste_buffer` in the transformation.
        std::optional<gc::Root<vm::Value>> callback =
            options_.editor_state.environment().ptr()->Lookup(
                options_.editor_state.gc_pool(), vm::Namespace(),
                L"HandleKeyboardControlU",
                {.variant = vm::types::Function{
                     .type_arguments = {
                         {.variant = vm::types::Void{}},
                         vm::GetVMType<gc::Root<OpenBuffer>>::vmtype()}}});
        if (!callback.has_value()) {
          LOG(WARNING) << "Didn't find HandleKeyboardControlU function.";
          return;
        }
        ForEachActiveBuffer(
            buffers_, {21}, [options = options_, callback](OpenBuffer& buffer) {
              std::vector<NonNull<std::unique_ptr<vm::Expression>>> args;
              args.push_back(vm::NewConstantExpression(
                  {VMTypeMapper<gc::Root<OpenBuffer>>::New(
                      buffer.editor().gc_pool(), buffer.NewRoot())}));
              NonNull<std::unique_ptr<vm::Expression>> expression =
                  vm::NewFunctionCall(
                      vm::NewConstantExpression(callback.value()),
                      std::move(args));
              if (expression->Types().empty()) {
                buffer.status().SetWarningText(
                    L"Unable to compile (type mismatch).");
                return futures::Past(EmptyValue());
              }
              return buffer
                  .EvaluateExpression(expression.value(),
                                      buffer.environment().ToRoot())
                  .ConsumeErrors([&pool = buffer.editor().gc_pool()](Error) {
                    return futures::Past(vm::Value::NewVoid(pool));
                  })
                  .Transform(ModifyHandler<gc::Root<vm::Value>>(
                      options.modify_handler, buffer));
            });
        return;
      }

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          WriteLineBuffer(buffers_, {22});
        } else {
          DLOG(INFO) << "Set literal.";
          status_expiration_for_literal_ =
              options_.editor_state.status().SetExpiringInformationText(
                  L"<literal>");
          return;
        }
        break;

      case Terminal::CTRL_K: {
        ResetScrollBehavior();
        StartNewInsertion();

        ForEachActiveBuffer(
            buffers_, {0x0b}, [options = options_](OpenBuffer& buffer) {
              return buffer
                  .ApplyToCursors(transformation::Delete{
                      .modifiers =
                          {.structure = StructureLine(),
                           .paste_buffer_behavior =
                               Modifiers::PasteBufferBehavior::kDoNothing,
                           .boundary_begin = Modifiers::CURRENT_POSITION,
                           .boundary_end = Modifiers::LIMIT_CURRENT},
                      .initiator = transformation::Delete::Initiator::kUser})
                  .Transform(ModifyHandler<EmptyValue>(options.modify_handler,
                                                       buffer));
            });
        return;
      }
    }
    ResetScrollBehavior();

    // TODO: Apply TransformKeyboardText for buffers with fd?
    ForEachActiveBuffer(
        buffers_, {static_cast<char>(c)},
        [c, options = options_](OpenBuffer& buffer) {
          return buffer.TransformKeyboardText(std::wstring(1, c))
              .Transform([options,
                          buffer_root = buffer.NewRoot()](std::wstring value) {
                VLOG(6) << "Inserting text: [" << value << "]";
                return buffer_root.ptr()->ApplyToCursors(transformation::Insert{
                    .contents_to_insert = MakeNonNullShared<BufferContents>(
                        MakeNonNullShared<Line>(value)),
                    .modifiers = {
                        .insertion =
                            options.editor_state.modifiers().insertion}});
              })
              .Transform(
                  ModifyHandler<EmptyValue>(options.modify_handler, buffer));
        });
    current_insertion_->AppendToLine(current_insertion_->EndLine(),
                                     Line(std::wstring(1, c)));
  }

  CursorMode cursor_mode() const override {
    switch (options_.editor_state.modifiers().insertion) {
      case Modifiers::ModifyMode::kShift:
        return CursorMode::kInserting;
      case Modifiers::ModifyMode::kOverwrite:
        return CursorMode::kOverwriting;
    }
    LOG(FATAL) << "Invalid cursor mode.";
    return CursorMode::kInserting;
  }

 private:
  // Writes `line_buffer` to every buffer with a fd, and runs `callable` in
  // every buffer without an fd.
  static futures::Value<EmptyValue> ForEachActiveBuffer(
      NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers,
      std::string line_buffer,
      std::function<futures::Value<EmptyValue>(OpenBuffer&)> callable) {
    return WriteLineBuffer(buffers, line_buffer)
        .Transform([buffers, callable](EmptyValue) {
          return futures::ForEach(
              buffers.get_shared(),
              [callable](gc::Root<OpenBuffer>& buffer_root) {
                return buffer_root.ptr()->fd() == nullptr
                           ? callable(buffer_root.ptr().value())
                                 .Transform([](EmptyValue) {
                                   return futures::IterationControlCommand::
                                       kContinue;
                                 })
                           : futures::Past(
                                 futures::IterationControlCommand::kContinue);
              });
        })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  void StartNewInsertion() {
    current_insertion_ = NewInsertion(options_.editor_state);
  }

  void HandleDelete(std::string line_buffer, Direction direction) {
    ResetScrollBehavior();
    ForEachActiveBuffer(
        buffers_, line_buffer,
        [direction, options = options_](OpenBuffer& buffer) {
          buffer.MaybeAdjustPositionCol();
          gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
          transformation::Stack stack;
          stack.PushBack(transformation::Delete{
              .modifiers = {.direction = direction,
                            .paste_buffer_behavior =
                                Modifiers::PasteBufferBehavior::kDoNothing},
              .initiator = transformation::Delete::Initiator::kUser});
          switch (options.editor_state.modifiers().insertion) {
            case Modifiers::ModifyMode::kShift:
              break;
            case Modifiers::ModifyMode::kOverwrite:
              stack.PushBack(transformation::Insert{
                  .contents_to_insert = MakeNonNullShared<BufferContents>(
                      MakeNonNullShared<const Line>(L" ")),
                  .final_position =
                      direction == Direction::kBackwards
                          ? transformation::Insert::FinalPosition::kStart
                          : transformation::Insert::FinalPosition::kEnd});
              break;
          }

          return buffer.ApplyToCursors(stack).Transform(
              ModifyHandler<EmptyValue>(options.modify_handler, buffer));
        });
    switch (direction) {
      case Direction::kBackwards:
        if (current_insertion_->back()->empty()) {
          StartNewInsertion();
        } else {
          current_insertion_->DeleteToLineEnd(
              current_insertion_->PositionBefore(
                  current_insertion_->range().end));
        }
        break;
      case Direction::kForwards:
        StartNewInsertion();
    }
  }

  void ApplyScrollBehavior(std::string line_buffer,
                           void (ScrollBehavior::*method)(OpenBuffer&)) {
    current_insertion_ = NewInsertion(options_.editor_state);
    GetScrollBehavior().AddListener(
        [buffers = buffers_, line_buffer,
         abort_value = scroll_behavior_abort_notification_->listenable_value(),
         method](NonNull<std::shared_ptr<ScrollBehavior>> scroll_behavior) {
          if (abort_value.has_value()) return;
          ForEachActiveBuffer(buffers, line_buffer,
                              [scroll_behavior, method](OpenBuffer& buffer) {
                                if (buffer.fd() == nullptr) {
                                  (scroll_behavior.value().*method)(buffer);
                                }
                                return futures::Past(EmptyValue());
                              });
        });
  }

  template <typename T>
  static std::function<futures::Value<EmptyValue>(const T&)> ModifyHandler(
      std::function<futures::Value<language::EmptyValue>(OpenBuffer&)> handler,
      OpenBuffer& buffer) {
    return [handler, buffer_root = buffer.NewRoot()](const T&) {
      return handler(buffer_root.ptr().value());
    };
  }

  futures::ListenableValue<NonNull<std::shared_ptr<ScrollBehavior>>>
  GetScrollBehavior() {
    if (!scroll_behavior_.has_value()) {
      scroll_behavior_ = futures::ListenableValue(
          options_.scroll_behavior
              ->Build(scroll_behavior_abort_notification_->listenable_value())
              .Transform(
                  [](NonNull<std::unique_ptr<ScrollBehavior>> scroll_behavior) {
                    return NonNull<std::shared_ptr<ScrollBehavior>>(
                        std::move(scroll_behavior));
                  }));
    }
    return scroll_behavior_.value();
  }

  void ResetScrollBehavior() {
    scroll_behavior_abort_notification_ =
        MakeNonNullUnique<DeleteNotification>();
    scroll_behavior_ = std::nullopt;
  }

  static futures::Value<EmptyValue> WriteLineBuffer(
      NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers,
      std::string line_buffer) {
    if (line_buffer.empty()) return futures::Past(EmptyValue());
    return futures::ForEach(
               buffers.get_shared(),
               [line_buffer =
                    std::move(line_buffer)](gc::Root<OpenBuffer>& buffer_root) {
                 OpenBuffer& buffer = buffer_root.ptr().value();
                 if (auto fd = buffer.fd(); fd != nullptr) {
                   if (write(fd->fd().read(), line_buffer.c_str(),
                             line_buffer.size()) == -1) {
                     buffer.status().SetWarningText(
                         L"Write failed: " + FromByteString(strerror(errno)));
                   } else {
                     buffer.editor().StartHandlingInterrupts();
                   }
                 }
                 return futures::Past(
                     futures::IterationControlCommand::kContinue);
               })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  const InsertModeOptions options_;

  // Copy of the contents of options_.buffers. shared_ptr to make it easy for it
  // to be captured efficiently.
  const NonNull<std::shared_ptr<std::vector<gc::Root<OpenBuffer>>>> buffers_;

  std::optional<
      futures::ListenableValue<NonNull<std::shared_ptr<ScrollBehavior>>>>
      scroll_behavior_;

  // If nullptr, next key should be interpreted directly. If non-null, next key
  // should be inserted literally.
  std::unique_ptr<StatusExpirationControl,
                  std::function<void(StatusExpirationControl*)>>
      status_expiration_for_literal_ = nullptr;

  // Given to ScrollBehaviorFactory::Build, and used to signal when we want to
  // abort the build of the history.
  NonNull<std::unique_ptr<DeleteNotification>>
      scroll_behavior_abort_notification_;

  std::unique_ptr<BufferContents, std::function<void(BufferContents*)>>
      current_insertion_;
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  for (gc::Root<OpenBuffer>& buffer_root : options.buffers.value()) {
    OpenBuffer& buffer = buffer_root.ptr().value();
    if (buffer.fd() == nullptr) continue;
    if (buffer.Read(buffer_variables::extend_lines)) {
      buffer.MaybeExtendLine(buffer.position());
    } else {
      buffer.MaybeAdjustPositionCol();
    }
  }
  for (gc::Root<OpenBuffer>& buffer_root : options.buffers.value()) {
    buffer_root.ptr()->status().SetInformationText(
        buffer_root.ptr()->fd() == nullptr ? L"🔡" : L"🔡 (raw)");
  }

  options.editor_state.set_keyboard_redirect(
      std::make_unique<InsertMode>(options));

  bool beep = false;
  for (gc::Root<OpenBuffer>& buffer_root : options.buffers.value()) {
    OpenBuffer& buffer = buffer_root.ptr().value();
    beep = buffer.active_cursors().size() > 1 &&
           buffer.Read(buffer_variables::multiple_cursors);
  }
  if (beep) {
    audio::BeepFrequencies(
        options.editor_state.audio_player(), 0.1,
        {audio::Frequency(659.25), audio::Frequency(1046.50)});
  }
}
}  // namespace

void DefaultScrollBehavior::Up(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = StructureLine(), .direction = Direction::kBackwards},
      NewMoveTransformation()});
}

void DefaultScrollBehavior::Down(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.structure = StructureLine()}, NewMoveTransformation()});
}

void DefaultScrollBehavior::Left(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::ModifiersAndComposite{
      {.direction = Direction::kBackwards}, NewMoveTransformation()});
}

void DefaultScrollBehavior::Right(OpenBuffer& buffer) {
  buffer.ApplyToCursors(NewMoveTransformation());
}

void DefaultScrollBehavior::Begin(OpenBuffer& buffer) {
  buffer.ApplyToCursors(transformation::SetPosition(ColumnNumber(0)));
}

void DefaultScrollBehavior::End(OpenBuffer& buffer) {
  buffer.ApplyToCursors(
      transformation::SetPosition(std::numeric_limits<ColumnNumber>::max()));
}

NonNull<std::unique_ptr<Command>> NewFindCompletionCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<FindCompletionCommand>(editor_state);
}

/* static */ NonNull<std::unique_ptr<ScrollBehaviorFactory>>
ScrollBehaviorFactory::Default() {
  class DefaultScrollBehaviorFactory : public ScrollBehaviorFactory {
    futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
        DeleteNotification::Value) override {
      return futures::Past(NonNull<std::unique_ptr<ScrollBehavior>>(
          MakeNonNullUnique<DefaultScrollBehavior>()));
    }
  };

  return MakeNonNullUnique<DefaultScrollBehaviorFactory>();
}

void EnterInsertMode(InsertModeOptions options) {
  auto shared_options = std::make_shared<InsertModeOptions>(std::move(options));

  if (!shared_options->buffers.has_value()) {
    shared_options->buffers = shared_options->editor_state.active_buffers();
  }

  (shared_options->buffers.value().empty()
       ? OpenAnonymousBuffer(shared_options->editor_state)
             .Transform([shared_options](gc::Root<OpenBuffer> buffer) {
               shared_options->buffers.value().push_back(buffer);
               return EmptyValue();
             })
       : futures::Past(EmptyValue()))
      .Transform([shared_options](EmptyValue) {
        for (gc::Root<OpenBuffer>& buffer_root :
             shared_options->buffers.value()) {
          if (std::optional<Line::BufferLineColumn> line_buffer =
                  buffer_root.ptr()->current_line()->buffer_line_column();
              line_buffer.has_value())
            VisitPointer(
                line_buffer->buffer.Lock(),
                [&](gc::Root<OpenBuffer> target_buffer) {
                  buffer_root = target_buffer;
                },
                [] {});
        }

        if (shared_options->modify_handler == nullptr) {
          shared_options->modify_handler = [](OpenBuffer&) {
            return futures::Past(EmptyValue()); /* Nothing. */
          };
        }

        if (!shared_options->escape_handler) {
          shared_options->escape_handler = []() { /* Nothing. */ };
        }

        if (!shared_options->new_line_handler) {
          shared_options->new_line_handler = [](OpenBuffer& buffer) {
            return buffer.ApplyToCursors(
                MakeNonNullUnique<NewLineTransformation>());
          };
        }

        if (!shared_options->start_completion) {
          shared_options->start_completion = [](OpenBuffer& buffer) {
            LOG(INFO) << "Start default completion.";
            buffer.ApplyToCursors(NewExpandTransformation());
            return true;
          };
        }

        shared_options->editor_state.status().Reset();
        for (gc::Root<OpenBuffer>& buffer : shared_options->buffers.value()) {
          buffer.ptr()->status().Reset();
        }

        if (shared_options->editor_state.structure() == StructureChar() ||
            shared_options->editor_state.structure() == StructureLine()) {
          for (gc::Root<OpenBuffer>& buffer_root :
               shared_options->buffers.value()) {
            OpenBuffer& buffer = buffer_root.ptr().value();
            buffer.CheckPosition();
            buffer.PushTransformationStack();
            buffer.PushTransformationStack();
          }
          if (shared_options->editor_state.structure() == StructureLine()) {
            for (gc::Root<OpenBuffer>& buffer :
                 shared_options->buffers.value()) {
              buffer.ptr()->ApplyToCursors(
                  MakeNonNullUnique<InsertEmptyLineTransformation>(
                      shared_options->editor_state.direction()));
            }
          }
          EnterInsertCharactersMode(*shared_options);
        }
        shared_options->editor_state.ResetDirection();
        shared_options->editor_state.ResetStructure();
        return EmptyValue();
      });
}

}  // namespace afc::editor
