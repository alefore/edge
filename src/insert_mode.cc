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
      auto buffer_to_insert =
          OpenBuffer::New({.editor = input.editor, .name = L"- text inserted"});
      buffer_to_insert->AppendRawLine(std::make_shared<Line>(
          Line::Options(*line).DeleteSuffix(prefix_end)));
      transformation::Insert insert_options;
      insert_options.buffer_to_insert = buffer_to_insert;
      output.Push(std::move(insert_options));
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
  wstring Description() const override {
    return L"Autocompletes the current word.";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    // TODO(multiple_buffers): Honor.
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->ApplyToCursors(NewExpandTransformation());
  }
};

class InsertMode : public EditorMode {
 public:
  InsertMode(InsertModeOptions options) : options_(std::move(options)) {
    CHECK(options_.escape_handler);
    CHECK(options_.buffers.has_value());
    CHECK(!options_.buffers.value().empty());
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    bool old_literal = literal_;
    literal_ = false;
    if (old_literal) {
      options_.editor_state->status()->Reset();
    }

    CHECK(options_.buffers.has_value());
    auto future = futures::Past(futures::IterationControlCommand::kContinue);
    switch (static_cast<int>(c)) {
      case '\t':
        ResetScrollBehavior();
        ForEachActiveBuffer(
            {'\t'},
            [options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
              // TODO: Don't ignore the return value. If it's false for all
              // buffers, insert the \t.
              options.start_completion(buffer);
              return futures::Past(EmptyValue());
            });
        return;

      case Terminal::ESCAPE:
        ResetScrollBehavior();
        futures::Transform(
            ForEachActiveBuffer(
                old_literal ? std::string{27} : "",
                [options = options_,
                 old_literal](const std::shared_ptr<OpenBuffer>& buffer) {
                  if (buffer->fd() != nullptr) {
                    if (old_literal) {
                      buffer->status()->SetInformationText(L"ESC");
                    } else {
                      buffer->status()->Reset();
                    }
                    return futures::Past(EmptyValue());
                  }
                  buffer->MaybeAdjustPositionCol();
                  // TODO(easy): Honor `old_literal`.
                  return futures::Transform(
                      buffer->ApplyToCursors(
                          NewDeleteSuffixSuperfluousCharacters()),
                      [options, buffer](EmptyValue) {
                        buffer->PopTransformationStack();
                        auto repetitions =
                            options.editor_state->repetitions().value_or(1);
                        if (repetitions > 0) {
                          options.editor_state->set_repetitions(repetitions -
                                                                1);
                        }
                        return buffer->RepeatLastTransformation();
                      },
                      [options, buffer](EmptyValue) {
                        buffer->PopTransformationStack();
                        options.editor_state->PushCurrentPosition();
                        buffer->status()->Reset();
                        return futures::Past(EmptyValue());
                      });
                }),
            [editor_state, options = options_, old_literal](EmptyValue) {
              if (old_literal) return futures::Past(EmptyValue());
              editor_state->status()->Reset();
              CHECK(options.escape_handler != nullptr);
              options.escape_handler();  // Probably deletes us.
              editor_state->ResetRepetitions();
              editor_state->ResetInsertionModifier();
              editor_state->set_keyboard_redirect(nullptr);
              return futures::Past(EmptyValue());
            });
        return;

      case Terminal::UP_ARROW:
        ApplyScrollBehavior({27, '[', 'A'}, &ScrollBehavior::Up, editor_state);
        return;

      case Terminal::DOWN_ARROW:
        ApplyScrollBehavior({27, '[', 'B'}, &ScrollBehavior::Down,
                            editor_state);
        return;

      case Terminal::LEFT_ARROW:
        ApplyScrollBehavior({27, '[', 'D'}, &ScrollBehavior::Left,
                            editor_state);
        return;

      case Terminal::RIGHT_ARROW:
        ApplyScrollBehavior({27, '[', 'C'}, &ScrollBehavior::Right,
                            editor_state);
        return;

      case Terminal::CTRL_A:
        ApplyScrollBehavior({1}, &ScrollBehavior::Begin, editor_state);
        return;

      case Terminal::CTRL_E:
        line_buffer_.push_back(5);
        ApplyScrollBehavior({5}, &ScrollBehavior::End, editor_state);
        return;

      case Terminal::CTRL_L:
        WriteLineBuffer({0x0c});
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
        ForEachActiveBuffer({'\n'}, options_.new_line_handler);
        return;

      case Terminal::CTRL_U: {
        ResetScrollBehavior();
        // TODO: Find a way to set `copy_to_paste_buffer` in the transformation.
        std::shared_ptr<Value> callback = editor_state->environment()->Lookup(
            Environment::Namespace(), L"HandleKeyboardControlU",
            VMType::Function(
                {VMType::Void(),
                 VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype}));
        if (callback == nullptr) {
          LOG(WARNING) << "Didn't find HandleKeyboardControlU function.";
          return;
        }
        ForEachActiveBuffer(
            {21}, [options = options_,
                   callback](const std::shared_ptr<OpenBuffer>& buffer) {
              std::vector<std::unique_ptr<vm::Expression>> args;
              args.push_back(vm::NewConstantExpression(
                  {VMTypeMapper<std::shared_ptr<OpenBuffer>>::New(buffer)}));
              std::shared_ptr<Expression> expression = vm::NewFunctionCall(
                  vm::NewConstantExpression(
                      std::make_unique<vm::Value>(*callback)),
                  std::move(args));
              if (expression->Types().empty()) {
                buffer->status()->SetWarningText(
                    L"Unable to compile (type mismatch).");
                return futures::Past(EmptyValue());
              }
              return CallModifyHandler(
                  options, buffer,
                  buffer->EvaluateExpression(expression.get(),
                                             buffer->environment()));
            });
        return;
      }

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          WriteLineBuffer({22});
        } else {
          DLOG(INFO) << "Set literal.";
          options_.editor_state->status()->SetInformationText(L"<literal>");
          literal_ = true;
        }
        break;

      case Terminal::CTRL_K: {
        ResetScrollBehavior();
        ForEachActiveBuffer(
            {0x0b},
            [options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
              return CallModifyHandler(
                  options, buffer,
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
        {static_cast<char>(c)},
        [c, options = options_](const std::shared_ptr<OpenBuffer>& buffer) {
          return futures::Transform(
              buffer->TransformKeyboardText(std::wstring(1, c)),
              [options, buffer](std::wstring value) {
                auto buffer_to_insert =
                    OpenBuffer::New({.editor = options.editor_state,
                                     .name = L"- text inserted"});

                buffer_to_insert->AppendToLastLine(NewLazyString(value));

                transformation::Insert insert_options(
                    std::move(buffer_to_insert));
                insert_options.modifiers.insertion =
                    options.editor_state->modifiers().insertion;
                return CallModifyHandler(
                    options, buffer,
                    buffer->ApplyToCursors(std::move(insert_options)));
              });
        });
  }

 private:
  // Writes `line_buffer` to every buffer with a fd, and runs `callable` in
  // every buffer without an fd.
  futures::Value<EmptyValue> ForEachActiveBuffer(
      std::string line_buffer, std::function<futures::Value<EmptyValue>(
                                   const std::shared_ptr<OpenBuffer>&)>
                                   callable) {
    return futures::Transform(
        WriteLineBuffer(line_buffer),
        [this, callable](EmptyValue) {
          return futures::ForEachWithCopy(
              options_.buffers.value().begin(), options_.buffers.value().end(),
              [callable](const std::shared_ptr<OpenBuffer>& buffer) {
                return buffer->fd() == nullptr
                           ? futures::Transform(
                                 callable(buffer),
                                 futures::Past(
                                     futures::IterationControlCommand::
                                         kContinue))
                           : futures::Past(
                                 futures::IterationControlCommand::kContinue);
              });
        },
        futures::Past(EmptyValue()));
  }

  void HandleDelete(std::string line_buffer, Direction direction) {
    ResetScrollBehavior();
    ForEachActiveBuffer(
        line_buffer, [direction, options = options_](
                         const std::shared_ptr<OpenBuffer>& buffer) {
          buffer->MaybeAdjustPositionCol();
          transformation::Delete delete_options;
          if (direction == Direction::kBackwards) {
            delete_options.modifiers.direction = Direction::kBackwards;
          }
          delete_options.modifiers.paste_buffer_behavior =
              Modifiers::PasteBufferBehavior::kDoNothing;
          delete_options.modifiers.delete_behavior =
              Modifiers::DeleteBehavior::kDeleteText;
          return futures::Transform(
              CallModifyHandler(
                  options, buffer,
                  buffer->ApplyToCursors(std::move(delete_options))),
              [options, direction, buffer](EmptyValue) {
                if (options.editor_state->modifiers().insertion !=
                    Modifiers::ModifyMode::kOverwrite)
                  return futures::Past(EmptyValue());

                auto buffer_to_insert =
                    OpenBuffer::New({.editor = options.editor_state,
                                     .name = L"- text inserted"});
                buffer_to_insert->AppendToLastLine(NewLazyString(L" "));
                transformation::Insert insert_options(
                    std::move(buffer_to_insert));
                if (direction == Direction::kBackwards) {
                  insert_options.final_position =
                      transformation::Insert::FinalPosition::kStart;
                }
                return buffer->ApplyToCursors(std::move(insert_options));
              },
              [handler = options.modify_handler, buffer](EmptyValue) {
                return handler(buffer);
              });
        });
  }

  void ApplyScrollBehavior(std::string line_buffer,
                           void (ScrollBehavior::*method)(EditorState*,
                                                          OpenBuffer*),
                           EditorState* editor_state) {
    GetScrollBehavior().AddListener(
        [this, line_buffer, editor_state,
         notification = scroll_behavior_abort_notification_,
         method](std::shared_ptr<ScrollBehavior> scroll_behavior) {
          if (notification->HasBeenNotified()) return;
          ForEachActiveBuffer(
              line_buffer, [editor_state, scroll_behavior,
                            method](const std::shared_ptr<OpenBuffer>& buffer) {
                if (buffer->fd() == nullptr) {
                  (scroll_behavior.get()->*method)(editor_state, buffer.get());
                }
                return futures::Past(EmptyValue());
              });
        });
  }

  template <typename T>
  static futures::Value<EmptyValue> CallModifyHandler(
      InsertModeOptions options, const std::shared_ptr<OpenBuffer>& buffer,
      futures::Value<T> value) {
    return futures::Transform(value, [options, buffer](const T&) {
      return options.modify_handler(buffer);
    });
  }

  futures::ListenableValue<std::shared_ptr<ScrollBehavior>>
  GetScrollBehavior() {
    if (!scroll_behavior_.has_value()) {
      CHECK(options_.scroll_behavior != nullptr);
      scroll_behavior_abort_notification_->Notify();
      scroll_behavior_abort_notification_ = std::make_shared<Notification>();
      scroll_behavior_ = futures::ListenableValue(futures::Transform(
          options_.scroll_behavior->Build(scroll_behavior_abort_notification_),
          [](std::unique_ptr<ScrollBehavior> scroll_behavior) {
            return std::shared_ptr<ScrollBehavior>(std::move(scroll_behavior));
          }));
    }
    return scroll_behavior_.value();
  }

  void ResetScrollBehavior() {
    scroll_behavior_abort_notification_->Notify();
    scroll_behavior_ = std::nullopt;
  }

  futures::Value<EmptyValue> WriteLineBuffer(std::string line_buffer) {
    if (line_buffer.empty()) return futures::Past(EmptyValue());
    return futures::Transform(
        futures::ForEachWithCopy(
            options_.buffers.value().begin(), options_.buffers.value().end(),
            [line_buffer = std::move(line_buffer)](
                const std::shared_ptr<OpenBuffer>& buffer) {
              if (auto fd = buffer->fd(); fd != nullptr) {
                if (write(fd->fd(), line_buffer.c_str(), line_buffer.size()) ==
                    -1) {
                  buffer->status()->SetWarningText(
                      L"Write failed: " + FromByteString(strerror(errno)));
                } else {
                  buffer->editor()->StartHandlingInterrupts();
                }
              }
              return futures::Past(futures::IterationControlCommand::kContinue);
            }),
        futures::Past(EmptyValue()));
  }

  const InsertModeOptions options_;
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
    buffer->status()->SetInformationText(buffer->fd() == nullptr ? L"🔡"
                                                                 : L"🔡 (raw)");
  }

  auto handler = std::make_unique<InsertMode>(options);
  options.editor_state->set_keyboard_redirect(std::move(handler));

  bool beep = false;
  for (auto& buffer : options.buffers.value()) {
    beep = buffer->active_cursors()->size() > 1 &&
           buffer->Read(buffer_variables::multiple_cursors);
  }
  if (beep) {
    BeepFrequencies(options.editor_state->audio_player(), {659.25, 1046.50});
  }
}
}  // namespace

void DefaultScrollBehavior::Up(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = Direction::kBackwards;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(transformation::ModifiersAndComposite{
      modifiers, NewMoveTransformation()});
}

void DefaultScrollBehavior::Down(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(transformation::ModifiersAndComposite{
      modifiers, NewMoveTransformation()});
}

void DefaultScrollBehavior::Left(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = Direction::kBackwards;
  buffer->ApplyToCursors(transformation::ModifiersAndComposite{
      modifiers, NewMoveTransformation()});
}

void DefaultScrollBehavior::Right(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(NewMoveTransformation());
}

void DefaultScrollBehavior::Begin(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(transformation::SetPosition(ColumnNumber(0)));
}

void DefaultScrollBehavior::End(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(
      transformation::SetPosition(std::numeric_limits<ColumnNumber>::max()));
}

std::unique_ptr<Command> NewFindCompletionCommand() {
  return std::make_unique<FindCompletionCommand>();
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

  EditorState* editor_state = shared_options->editor_state;
  CHECK(editor_state != nullptr);

  if (!shared_options->buffers.has_value()) {
    shared_options->buffers = editor_state->active_buffers();
  }

  auto anonymous_buffer_future = futures::Past(EmptyValue());
  if (shared_options->buffers.value().empty()) {
    anonymous_buffer_future = futures::Transform(
        OpenAnonymousBuffer(editor_state),
        [shared_options](std::shared_ptr<OpenBuffer> buffer) {
          shared_options->buffers.value().push_back(buffer);
          return EmptyValue();
        });
  }

  futures::Transform(anonymous_buffer_future, [editor_state,
                                               shared_options](EmptyValue) {
    for (auto& buffer : shared_options->buffers.value()) {
      auto target_buffer = buffer->GetBufferFromCurrentLine();
      if (target_buffer != nullptr) {
        buffer = target_buffer;
      }
    }

    if (!shared_options->modify_handler) {
      shared_options->modify_handler = [](const std::shared_ptr<OpenBuffer>&) {
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

    shared_options->editor_state->status()->Reset();
    for (auto& buffer : shared_options->buffers.value()) {
      buffer->status()->Reset();
    }

    if (editor_state->structure() == StructureChar() ||
        editor_state->structure() == StructureLine()) {
      for (auto& buffer : shared_options->buffers.value()) {
        buffer->CheckPosition();
        buffer->PushTransformationStack();
        buffer->PushTransformationStack();
      }
      if (editor_state->structure() == StructureLine()) {
        for (auto& buffer : shared_options->buffers.value()) {
          buffer->ApplyToCursors(
              std::make_unique<InsertEmptyLineTransformation>(
                  editor_state->direction()));
        }
      }
      EnterInsertCharactersMode(*shared_options);
    }
    editor_state->ResetDirection();
    editor_state->ResetStructure();
    return EmptyValue();
  });
}

}  // namespace afc::editor
