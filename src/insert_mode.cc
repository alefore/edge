#include "src/insert_mode.h"

#include <algorithm>
#include <memory>
#include <vector>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/futures/futures.h"
#include "src/lazy_string_append.h"
#include "src/notification.h"
#include "src/parse_tree.h"
#include "src/substring.h"
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
#include "src/wstring.h"

namespace afc::editor {
namespace {
class NewLineTransformation : public CompositeTransformation {
  std::wstring Serialize() const override { return L"NewLineTransformation()"; }
  futures::Value<Output> Apply(Input input) const override {
    const ColumnNumber column = input.position.column;
    auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    if (input.buffer->Read(buffer_variables::atomic_lines) &&
        column != ColumnNumber(0) && column != line->EndColumn())
      return futures::Past(Output());
    const wstring& line_prefix_characters(
        input.buffer->Read(buffer_variables::line_prefix_characters));
    ColumnNumber prefix_end;
    if (!input.buffer->Read(buffer_variables::paste_mode)) {
      while (prefix_end < column &&
             (line_prefix_characters.find(line->get(prefix_end)) !=
              line_prefix_characters.npos)) {
        ++prefix_end;
      }
    }

    Output output;
    {
      auto contents_to_insert = std::make_unique<BufferContents>();
      contents_to_insert->push_back(std::make_shared<Line>(
          Line::Options(*line).DeleteSuffix(prefix_end)));
      output.Push(transformation::Insert{.contents_to_insert =
                                             std::move(contents_to_insert)});
    }

    output.Push(transformation::SetPosition(input.position));
    output.Push(NewDeleteSuffixSuperfluousCharacters());
    output.Push(transformation::SetPosition(
        LineColumn(input.position.line + LineNumberDelta(1), prefix_end)));
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<NewLineTransformation>();
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
        Modifiers(), std::make_unique<NewLineTransformation>()});
    output.Push(transformation::SetPosition(input.position));
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<InsertEmptyLineTransformation>(direction_);
  }

 private:
  Direction direction_;
};

class FindCompletionCommand : public Command {
 public:
  FindCompletionCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}
  wstring Description() const override {
    return L"Autocompletes the current word.";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) {
    // TODO(multiple_buffers): Honor.
    auto buffer = editor_state_.current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->ApplyToCursors(NewExpandTransformation());
  }

 private:
  EditorState& editor_state_;
};

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options)
      : options_(std::move(options)),
        buffers_(std::make_shared<std::vector<std::shared_ptr<OpenBuffer>>>(
            std::vector<std::shared_ptr<OpenBuffer>>(
                options_.buffers->begin(), options_.buffers->end()))) {
    CHECK(options_.escape_handler);
    CHECK(options_.buffers.has_value());
    CHECK(!options_.buffers.value().empty());
  }

  void ProcessInput(wint_t c) {
    bool old_literal = literal_;
    literal_ = false;
    if (old_literal) {
      options_.editor_state.status().Reset();
    }

    CHECK(options_.buffers.has_value());
    auto future = futures::Past(futures::IterationControlCommand::kContinue);
    switch (static_cast<int>(c)) {
      case '\t':
        ResetScrollBehavior();
        ForEachActiveBuffer(
            buffers_, {'\t'},
            [options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
              // TODO: Don't ignore the return value. If it's false for all
              // buffers, insert the \t.
              options.start_completion(buffer);
              return futures::Past(EmptyValue());
            });
        return;

      case Terminal::ESCAPE:
        ResetScrollBehavior();

        ForEachActiveBuffer(
            buffers_, old_literal ? std::string{27} : "",
            [options = options_,
             old_literal](const std::shared_ptr<OpenBuffer>& buffer) {
              if (buffer->fd() != nullptr) {
                if (old_literal) {
                  buffer->status().SetInformationText(L"ESC");
                } else {
                  buffer->status().Reset();
                }
                return futures::Past(EmptyValue());
              }
              buffer->MaybeAdjustPositionCol();
              // TODO(easy): Honor `old_literal`.
              return buffer
                  ->ApplyToCursors(NewDeleteSuffixSuperfluousCharacters())
                  .Transform([options, buffer](EmptyValue) {
                    buffer->PopTransformationStack();
                    auto repetitions =
                        options.editor_state.repetitions().value_or(1);
                    if (repetitions > 0) {
                      options.editor_state.set_repetitions(repetitions - 1);
                    }
                    return buffer->RepeatLastTransformation();
                  })
                  .Transform([options, buffer](EmptyValue) {
                    buffer->PopTransformationStack();
                    options.editor_state.PushCurrentPosition();
                    buffer->status().Reset();
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
        line_buffer_.push_back(5);
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
        ForEachActiveBuffer(buffers_, {'\n'}, options_.new_line_handler);
        return;

      case Terminal::CTRL_U: {
        ResetScrollBehavior();
        // TODO: Find a way to set `copy_to_paste_buffer` in the transformation.
        std::shared_ptr<Value> callback =
            options_.editor_state.environment()->Lookup(
                Environment::Namespace(), L"HandleKeyboardControlU",
                VMType::Function(
                    {VMType::Void(),
                     VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype}));
        if (callback == nullptr) {
          LOG(WARNING) << "Didn't find HandleKeyboardControlU function.";
          return;
        }
        ForEachActiveBuffer(
            buffers_, {21},
            [options = options_,
             callback](const std::shared_ptr<OpenBuffer>& buffer) {
              std::vector<std::unique_ptr<vm::Expression>> args;
              args.push_back(vm::NewConstantExpression(
                  {VMTypeMapper<std::shared_ptr<OpenBuffer>>::New(buffer)}));
              std::shared_ptr<Expression> expression = vm::NewFunctionCall(
                  vm::NewConstantExpression(
                      std::make_unique<vm::Value>(*callback)),
                  std::move(args));
              if (expression->Types().empty()) {
                buffer->status().SetWarningText(
                    L"Unable to compile (type mismatch).");
                return futures::Past(EmptyValue());
              }
              return CallModifyHandler(
                  options, *buffer,
                  buffer
                      ->EvaluateExpression(expression.get(),
                                           buffer->environment())
                      .ConsumeErrors(
                          [](Error) { return futures::Past(nullptr); }));
            });
        return;
      }

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          WriteLineBuffer(buffers_, {22});
        } else {
          DLOG(INFO) << "Set literal.";
          options_.editor_state.status().SetInformationText(L"<literal>");
          literal_ = true;
        }
        break;

      case Terminal::CTRL_K: {
        ResetScrollBehavior();
        ForEachActiveBuffer(
            buffers_, {0x0b},
            [options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
              return CallModifyHandler(
                  options, *buffer,
                  buffer->ApplyToCursors(transformation::Delete{
                      .modifiers = {
                          .structure = StructureLine(),
                          .delete_behavior =
                              Modifiers::DeleteBehavior::kDeleteText,
                          .paste_buffer_behavior =
                              Modifiers::PasteBufferBehavior::kDoNothing,
                          .boundary_begin = Modifiers::CURRENT_POSITION,
                          .boundary_end = Modifiers::LIMIT_CURRENT}}));
            });
        return;
      }
    }
    ResetScrollBehavior();

    // TODO: Apply TransformKeyboardText for buffers with fd?
    ForEachActiveBuffer(
        buffers_, {static_cast<char>(c)},
        [c, options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
          return buffer->TransformKeyboardText(std::wstring(1, c))
              .Transform([options, buffer](std::wstring value) {
                auto buffer_to_insert =
                    OpenBuffer::New({.editor = options.editor_state,
                                     .name = BufferName::TextInsertion()});
                buffer_to_insert->AppendToLastLine(NewLazyString(value));

                return CallModifyHandler(
                    options, *buffer,
                    buffer->ApplyToCursors(transformation::Insert{
                        .contents_to_insert =
                            buffer_to_insert->contents().copy(),
                        .modifiers = {
                            .insertion =
                                options.editor_state.modifiers().insertion}}));
              });
        });
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
      std::shared_ptr<std::vector<std::shared_ptr<OpenBuffer>>> buffers,
      std::string line_buffer,
      std::function<
          futures::Value<EmptyValue>(const std::shared_ptr<OpenBuffer>&)>
          callable) {
    return WriteLineBuffer(buffers, line_buffer)
        .Transform([buffers, callable](EmptyValue) {
          return futures::ForEach(buffers, [callable](const std::shared_ptr<
                                                      OpenBuffer>& buffer) {
            return buffer->fd() == nullptr
                       ? callable(buffer).Transform([](EmptyValue) {
                           return futures::IterationControlCommand::kContinue;
                         })
                       : futures::Past(
                             futures::IterationControlCommand::kContinue);
          });
        })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  void HandleDelete(std::string line_buffer, Direction direction) {
    ResetScrollBehavior();
    ForEachActiveBuffer(
        buffers_, line_buffer,
        [direction,
         options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
          buffer->MaybeAdjustPositionCol();
          transformation::Delete delete_options;
          if (direction == Direction::kBackwards) {
            delete_options.modifiers.direction = Direction::kBackwards;
          }
          delete_options.modifiers.paste_buffer_behavior =
              Modifiers::PasteBufferBehavior::kDoNothing;
          delete_options.modifiers.delete_behavior =
              Modifiers::DeleteBehavior::kDeleteText;
          return CallModifyHandler(
                     options, *buffer,
                     buffer->ApplyToCursors(std::move(delete_options)))
              .Transform([options, direction, buffer](EmptyValue) {
                if (options.editor_state.modifiers().insertion !=
                    Modifiers::ModifyMode::kOverwrite)
                  return futures::Past(EmptyValue());

                return buffer->ApplyToCursors(transformation::Insert{
                    .contents_to_insert = std::make_unique<BufferContents>(
                        std::make_shared<Line>(L" ")),
                    .final_position =
                        direction == Direction::kBackwards
                            ? transformation::Insert::FinalPosition::kStart
                            : transformation::Insert::FinalPosition::kEnd});
              })
              // TODO:Why call modify_handler here? Isn't it redundant with
              // CallModifyHandler above?
              .Transform([handler = options.modify_handler,
                          buffer](EmptyValue) { return handler(*buffer); });
        });
  }

  void ApplyScrollBehavior(std::string line_buffer,
                           void (ScrollBehavior::*method)(OpenBuffer&)) {
    GetScrollBehavior().AddListener(
        [buffers = buffers_, line_buffer,
         notification = scroll_behavior_abort_notification_,
         method](std::shared_ptr<ScrollBehavior> scroll_behavior) {
          if (notification->HasBeenNotified()) return;
          ForEachActiveBuffer(buffers, line_buffer,
                              [scroll_behavior, method](
                                  const std::shared_ptr<OpenBuffer>& buffer) {
                                CHECK(buffer != nullptr);
                                if (buffer->fd() == nullptr) {
                                  (scroll_behavior.get()->*method)(*buffer);
                                }
                                return futures::Past(EmptyValue());
                              });
        });
  }

  template <typename T>
  static futures::Value<EmptyValue> CallModifyHandler(InsertModeOptions options,
                                                      OpenBuffer& buffer,
                                                      futures::Value<T> value) {
    return value.Transform(
        [options, buffer = buffer.shared_from_this()](const T&) {
          return options.modify_handler(*buffer);
        });
  }

  futures::ListenableValue<std::shared_ptr<ScrollBehavior>>
  GetScrollBehavior() {
    if (!scroll_behavior_.has_value()) {
      CHECK(options_.scroll_behavior != nullptr);
      scroll_behavior_abort_notification_->Notify();
      scroll_behavior_abort_notification_ = std::make_shared<Notification>();
      scroll_behavior_ = futures::ListenableValue(
          options_.scroll_behavior->Build(scroll_behavior_abort_notification_)
              .Transform([](std::unique_ptr<ScrollBehavior> scroll_behavior) {
                return std::shared_ptr<ScrollBehavior>(
                    std::move(scroll_behavior));
              }));
    }
    return scroll_behavior_.value();
  }

  void ResetScrollBehavior() {
    scroll_behavior_abort_notification_->Notify();
    scroll_behavior_ = std::nullopt;
  }

  static futures::Value<EmptyValue> WriteLineBuffer(
      std::shared_ptr<std::vector<std::shared_ptr<OpenBuffer>>> buffers,
      std::string line_buffer) {
    if (line_buffer.empty()) return futures::Past(EmptyValue());
    return futures::ForEach(
               buffers,
               [line_buffer = std::move(line_buffer)](
                   const std::shared_ptr<OpenBuffer>& buffer) {
                 if (auto fd = buffer->fd(); fd != nullptr) {
                   if (write(fd->fd(), line_buffer.c_str(),
                             line_buffer.size()) == -1) {
                     buffer->status().SetWarningText(
                         L"Write failed: " + FromByteString(strerror(errno)));
                   } else {
                     buffer->editor().StartHandlingInterrupts();
                   }
                 }
                 return futures::Past(
                     futures::IterationControlCommand::kContinue);
               })
        .Transform(
            [](futures::IterationControlCommand) { return EmptyValue(); });
  }

  const InsertModeOptions options_;
  // Copy of the contents of options_.buffers. Never null. shared_ptr to make it
  // easy for it to be captured efficiently.
  std::shared_ptr<std::vector<std::shared_ptr<OpenBuffer>>> buffers_;
  std::optional<futures::ListenableValue<std::shared_ptr<ScrollBehavior>>>
      scroll_behavior_;

  // For input to buffers that have a file descriptor, buffer the characters
  // here. This gets flushed upon certain presses, such as ESCAPE or new line.
  string line_buffer_;
  bool literal_ = false;

  // Given to ScrollBehaviorFactory::Build, and used to signal when we want to
  // abort the build of the history.
  std::shared_ptr<Notification> scroll_behavior_abort_notification_ =
      std::make_shared<Notification>();
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  for (auto& buffer : options.buffers.value()) {
    if (buffer->fd() == nullptr) continue;
    if (buffer->Read(buffer_variables::extend_lines)) {
      buffer->MaybeExtendLine(buffer->position());
    } else {
      buffer->MaybeAdjustPositionCol();
    }
  }
  for (auto& buffer : options.buffers.value()) {
    buffer->status().SetInformationText(buffer->fd() == nullptr ? L"🔡"
                                                                : L"🔡 (raw)");
  }

  options.editor_state.set_keyboard_redirect(
      std::make_unique<InsertMode>(options));

  bool beep = false;
  for (auto& buffer : options.buffers.value()) {
    beep = buffer->active_cursors()->size() > 1 &&
           buffer->Read(buffer_variables::multiple_cursors);
  }
  if (beep) {
    BeepFrequencies(options.editor_state.audio_player(), {659.25, 1046.50});
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

std::unique_ptr<Command> NewFindCompletionCommand(EditorState& editor_state) {
  return std::make_unique<FindCompletionCommand>(editor_state);
}

/* static */ std::unique_ptr<ScrollBehaviorFactory>
ScrollBehaviorFactory::Default() {
  class DefaultScrollBehaviorFactory : public ScrollBehaviorFactory {
    futures::Value<std::unique_ptr<ScrollBehavior>> Build(
        std::shared_ptr<Notification>) override {
      return futures::Past(
          std::unique_ptr<ScrollBehavior>(new DefaultScrollBehavior()));
    }
  };

  return std::make_unique<DefaultScrollBehaviorFactory>();
}

void EnterInsertMode(InsertModeOptions options) {
  auto shared_options = std::make_shared<InsertModeOptions>(std::move(options));

  if (!shared_options->buffers.has_value()) {
    shared_options->buffers = shared_options->editor_state.active_buffers();
  }

  auto anonymous_buffer_future = futures::Past(EmptyValue());
  if (shared_options->buffers.value().empty()) {
    anonymous_buffer_future =
        OpenAnonymousBuffer(shared_options->editor_state)
            .Transform([shared_options](std::shared_ptr<OpenBuffer> buffer) {
              shared_options->buffers.value().push_back(buffer);
              return EmptyValue();
            });
  }

  anonymous_buffer_future.Transform([shared_options](EmptyValue) {
    for (auto& buffer : shared_options->buffers.value()) {
      auto target_buffer = buffer->GetBufferFromCurrentLine();
      if (target_buffer != nullptr) {
        buffer = target_buffer;
      }
    }

    if (shared_options->modify_handler == nullptr) {
      shared_options->modify_handler = [](OpenBuffer&) {
        return futures::Past(EmptyValue()); /* Nothing. */
      };
    }

    if (shared_options->scroll_behavior == nullptr) {
      shared_options->scroll_behavior = ScrollBehaviorFactory::Default();
    }

    if (!shared_options->escape_handler) {
      shared_options->escape_handler = []() { /* Nothing. */ };
    }

    if (!shared_options->new_line_handler) {
      shared_options->new_line_handler =
          [](const std::shared_ptr<OpenBuffer>& buffer) {
            return buffer->ApplyToCursors(
                std::make_unique<NewLineTransformation>());
          };
    }

    if (!shared_options->start_completion) {
      shared_options->start_completion =
          [](const std::shared_ptr<OpenBuffer>& buffer) {
            LOG(INFO) << "Start default completion.";
            buffer->ApplyToCursors(NewExpandTransformation());
            return true;
          };
    }

    shared_options->editor_state.status().Reset();
    for (auto& buffer : shared_options->buffers.value()) {
      buffer->status().Reset();
    }

    if (shared_options->editor_state.structure() == StructureChar() ||
        shared_options->editor_state.structure() == StructureLine()) {
      for (auto& buffer : shared_options->buffers.value()) {
        buffer->CheckPosition();
        buffer->PushTransformationStack();
        buffer->PushTransformationStack();
      }
      if (shared_options->editor_state.structure() == StructureLine()) {
        for (auto& buffer : shared_options->buffers.value()) {
          buffer->ApplyToCursors(
              std::make_unique<InsertEmptyLineTransformation>(
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
