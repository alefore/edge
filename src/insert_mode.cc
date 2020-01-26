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
          std::make_shared<OpenBuffer>(input.editor, L"- text inserted");
      buffer_to_insert->AppendRawLine(std::make_shared<Line>(
          Line::Options(*line).DeleteSuffix(prefix_end)));
      InsertOptions insert_options;
      insert_options.buffer_to_insert = buffer_to_insert;
      output.Push(NewInsertBufferTransformation(std::move(insert_options)));
    }

    output.Push(NewSetPositionTransformation(input.position));
    output.Push(NewDeleteSuffixSuperfluousCharacters());
    output.Push(NewSetPositionTransformation(
        LineColumn(input.position.line + LineNumberDelta(1), prefix_end)));
    return futures::Past(std::move(output));
  }

  unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<NewLineTransformation>();
  }
};

class InsertEmptyLineTransformation : public CompositeTransformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}
  std::wstring Serialize() const override { return L""; }
  futures::Value<Output> Apply(Input input) const override {
    if (direction_ == BACKWARDS) {
      ++input.position.line;
    }
    Output output = Output::SetPosition(LineColumn(input.position.line));
    output.Push(NewTransformation(Modifiers(),
                                  std::make_unique<NewLineTransformation>()));
    output.Push(NewSetPositionTransformation(input.position));
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
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    CHECK(options_.buffers.has_value());
    auto value = futures::ForEach(
        options_.buffers.value().begin(), options_.buffers.value().end(),
        [this, c, editor_state](const std::shared_ptr<OpenBuffer>& buffer) {
          return futures::Transform(
              DeliverInputToBuffer(c, editor_state, buffer),
              futures::Past(futures::IterationControlCommand::kContinue));
        });
    if (static_cast<int>(c) == Terminal::ESCAPE) {
      value.SetConsumer(
          [editor_state, escape_handler = options_.escape_handler](
              futures::IterationControlCommand) {
            editor_state->status()->Reset();
            CHECK(escape_handler != nullptr);
            escape_handler();  // Probably deletes us.
            editor_state->ResetRepetitions();
            editor_state->ResetInsertionModifier();
            editor_state->current_buffer()->ResetMode();
            editor_state->set_keyboard_redirect(nullptr);
          });
    }
  }

 private:
  template <typename T>
  futures::Value<bool> CallModifyHandler(
      const std::shared_ptr<OpenBuffer>& buffer, futures::Value<T> value) {
    return futures::Transform(value, [this, buffer](const T&) {
      return options_.modify_handler(buffer);
    });
  }

  futures::Value<bool> DeliverInputToBuffer(
      wint_t c, EditorState* editor_state,
      const std::shared_ptr<OpenBuffer>& buffer) {
    CHECK(buffer != nullptr);
    switch (static_cast<int>(c)) {
      case '\t':
        ResetScrollBehavior();
        if (options_.start_completion(buffer)) {
          LOG(INFO) << "Completion has started, avoid inserting '\\t'.";
          return futures::Past(true);
        }
        break;

      case Terminal::ESCAPE:
        ResetScrollBehavior();
        buffer->MaybeAdjustPositionCol();
        return futures::Transform(
            buffer->ApplyToCursors(NewDeleteSuffixSuperfluousCharacters()),
            [buffer, editor_state, this](bool) {
              buffer->PopTransformationStack();
              editor_state->set_repetitions(editor_state->repetitions() - 1);
              return buffer->RepeatLastTransformation();
            },
            [buffer, editor_state, this](bool) {
              buffer->PopTransformationStack();
              editor_state->PushCurrentPosition();
              buffer->status()->Reset();
              return futures::Past(true);
            });

      case Terminal::UP_ARROW:
        GetScrollBehavior()->Up(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::DOWN_ARROW:
        GetScrollBehavior()->Down(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::LEFT_ARROW:
        GetScrollBehavior()->Left(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::RIGHT_ARROW:
        GetScrollBehavior()->Right(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::CTRL_A:
        GetScrollBehavior()->Begin(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::CTRL_E:
        GetScrollBehavior()->End(editor_state, buffer.get());
        return futures::Past(true);

      case Terminal::CTRL_D:
      case Terminal::DELETE:
      case Terminal::BACKSPACE: {
        ResetScrollBehavior();
        LOG(INFO) << "Handling backspace in insert mode.";
        buffer->MaybeAdjustPositionCol();
        DeleteOptions delete_options;
        if (c == wint_t(Terminal::BACKSPACE)) {
          delete_options.modifiers.direction = BACKWARDS;
        }
        delete_options.modifiers.paste_buffer_behavior =
            Modifiers::PasteBufferBehavior::kDoNothing;
        return futures::Transform(
            CallModifyHandler(buffer,
                              buffer->ApplyToCursors(NewDeleteTransformation(
                                  std::move(delete_options)))),
            [editor_state, c, buffer](bool) {
              if (editor_state->modifiers().insertion !=
                  Modifiers::ModifyMode::kOverwrite)
                return futures::Past(true);

              auto buffer_to_insert = std::make_shared<OpenBuffer>(
                  editor_state, L"- text inserted");
              buffer_to_insert->AppendToLastLine(NewLazyString(L" "));
              InsertOptions insert_options;
              insert_options.buffer_to_insert = buffer_to_insert;
              if (c == wint_t(Terminal::BACKSPACE)) {
                insert_options.final_position =
                    InsertOptions::FinalPosition::kStart;
              }
              return buffer->ApplyToCursors(
                  NewInsertBufferTransformation(std::move(insert_options)));
            },
            [handler = options_.modify_handler, buffer](bool) {
              return handler(buffer);
            });
      }

      case '\n':
        ResetScrollBehavior();
        return options_.new_line_handler(buffer);

      case Terminal::CTRL_U: {
        ResetScrollBehavior();
        // TODO: Find a way to set `copy_to_paste_buffer` in the
        // transformation.
        Value* callback = buffer->environment()->Lookup(
            L"HandleKeyboardControlU",
            VMType::Function(
                {VMType::Void(),
                 VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype}));
        if (callback == nullptr) {
          LOG(INFO) << "Didn't find HandleKeyboardControlU function: "
                    << buffer->Read(buffer_variables::name);
          return futures::Past(true);
        }
        std::vector<std::unique_ptr<vm::Expression>> args;
        args.push_back(vm::NewConstantExpression(
            {VMTypeMapper<std::shared_ptr<OpenBuffer>>::New(buffer)}));
        std::shared_ptr<Expression> expression = vm::NewFunctionCall(
            vm::NewConstantExpression(std::make_unique<vm::Value>(*callback)),
            std::move(args));
        if (expression->Types().empty()) {
          buffer->status()->SetWarningText(
              L"Unable to compile (type mismatch).");
          return futures::Past(true);
        }
        return CallModifyHandler(buffer,
                                 buffer->EvaluateExpression(expression.get()));
      }

      case Terminal::CTRL_K: {
        ResetScrollBehavior();
        DeleteOptions delete_options;
        delete_options.modifiers.structure = StructureLine();
        delete_options.modifiers.boundary_begin = Modifiers::CURRENT_POSITION;
        delete_options.modifiers.boundary_end = Modifiers::LIMIT_CURRENT;
        delete_options.modifiers.paste_buffer_behavior =
            Modifiers::PasteBufferBehavior::kDoNothing;
        return CallModifyHandler(
            buffer,
            buffer->ApplyToCursors(NewDeleteTransformation(delete_options)));
      }

      default:
        ResetScrollBehavior();
    }

    return futures::Transform(
        buffer->TransformKeyboardText(wstring(1, c)),
        [this, editor_state, buffer](std::wstring value) {
          auto buffer_to_insert =
              std::make_shared<OpenBuffer>(editor_state, L"- text inserted");

          buffer_to_insert->AppendToLastLine(NewLazyString(value));

          InsertOptions insert_options;
          insert_options.modifiers.insertion =
              editor_state->modifiers().insertion;
          insert_options.buffer_to_insert = buffer_to_insert;

          return CallModifyHandler(
              buffer, buffer->ApplyToCursors(NewInsertBufferTransformation(
                          std::move(insert_options))));
        });
  }

  ScrollBehavior* GetScrollBehavior() {
    if (scroll_behavior_ == nullptr) {
      CHECK(options_.scroll_behavior != nullptr);
      scroll_behavior_ = options_.scroll_behavior->Build();
    }
    return scroll_behavior_.get();
  }

  void ResetScrollBehavior() { scroll_behavior_ = nullptr; }

  const InsertModeOptions options_;
  std::unique_ptr<ScrollBehavior> scroll_behavior_;
};

class RawInputTypeMode : public EditorMode {
 public:
  RawInputTypeMode(shared_ptr<OpenBuffer> buffer) : buffer_(buffer) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    bool old_literal = literal_;
    literal_ = false;
    switch (static_cast<int>(c)) {
      case Terminal::CTRL_A:
        line_buffer_.push_back(1);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_D:
        line_buffer_.push_back(4);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_E:
        line_buffer_.push_back(5);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_K:
        line_buffer_.push_back(0x0b);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_L:
        line_buffer_.push_back(0x0c);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::CTRL_V:
        if (old_literal) {
          DLOG(INFO) << "Inserting literal CTRL_V";
          line_buffer_.push_back(22);
          WriteLineBuffer(editor_state);
        } else {
          DLOG(INFO) << "Set literal.";
          buffer_->status()->SetInformationText(L"<literal>");
          literal_ = true;
        }
        break;

      case Terminal::CTRL_U:
        if (!buffer_) {
          line_buffer_ = "";
        } else {
          line_buffer_.push_back(21);
          WriteLineBuffer(editor_state);
        }
        break;

      case Terminal::ESCAPE:
        if (old_literal) {
          buffer_->status()->SetInformationText(L"ESC");
          line_buffer_.push_back(27);
          WriteLineBuffer(editor_state);
        } else {
          editor_state->current_buffer()->ResetMode();
          editor_state->set_keyboard_redirect(nullptr);
          buffer_->status()->Reset();
          editor_state->status()->Reset();
        }
        break;

      case Terminal::UP_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('A');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::DOWN_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('B');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::RIGHT_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('C');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::LEFT_ARROW:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back('D');
        WriteLineBuffer(editor_state);
        break;

      case Terminal::DELETE:
        line_buffer_.push_back(27);
        line_buffer_.push_back('[');
        line_buffer_.push_back(51);
        line_buffer_.push_back(126);
        WriteLineBuffer(editor_state);
        break;

      case Terminal::BACKSPACE: {
        string contents(1, 127);
        if (buffer_->fd() != nullptr) {
          write(buffer_->fd()->fd(), contents.c_str(), contents.size());
        }
      } break;

      case '\n':
        line_buffer_.push_back('\n');
        WriteLineBuffer(editor_state);
        break;

      default:
        line_buffer_.push_back(c);
        WriteLineBuffer(editor_state);
    };
  }

 private:
  void WriteLineBuffer(EditorState* editor_state) {
    if (buffer_->fd() == nullptr) {
      buffer_->status()->SetWarningText(L"Warning: Process exited.");
    } else if (write(buffer_->fd()->fd(), line_buffer_.c_str(),
                     line_buffer_.size()) == -1) {
      buffer_->status()->SetWarningText(L"Write failed: " +
                                        FromByteString(strerror(errno)));
    } else {
      editor_state->StartHandlingInterrupts();
    }
    line_buffer_ = "";
  }

  // The buffer that we will be insertint into.
  const shared_ptr<OpenBuffer> buffer_;
  string line_buffer_;
  bool literal_ = false;
};

void EnterInsertCharactersMode(InsertModeOptions options) {
  for (auto& buffer : options.buffers.value()) {
    if (buffer->Read(buffer_variables::extend_lines)) {
      buffer->MaybeExtendLine(buffer->position());
    } else {
      buffer->MaybeAdjustPositionCol();
    }
    buffer->status()->SetInformationText(L"🔡");
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
using std::shared_ptr;
using std::unique_ptr;

void DefaultScrollBehavior::Up(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = BACKWARDS;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Down(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.structure = StructureLine();
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Left(EditorState*, OpenBuffer* buffer) {
  Modifiers modifiers;
  modifiers.direction = BACKWARDS;
  buffer->ApplyToCursors(NewMoveTransformation(modifiers));
}

void DefaultScrollBehavior::Right(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(NewMoveTransformation(Modifiers()));
}

void DefaultScrollBehavior::Begin(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(
      NewSetPositionTransformation(std::nullopt, ColumnNumber(0)));
}

void DefaultScrollBehavior::End(EditorState*, OpenBuffer* buffer) {
  buffer->ApplyToCursors(NewSetPositionTransformation(
      std::nullopt, std::numeric_limits<ColumnNumber>::max()));
}

std::unique_ptr<Command> NewFindCompletionCommand() {
  return std::make_unique<FindCompletionCommand>();
}

/* static */ std::unique_ptr<ScrollBehaviorFactory>
ScrollBehaviorFactory::Default() {
  class DefaultScrollBehaviorFactory : public ScrollBehaviorFactory {
    std::unique_ptr<ScrollBehavior> Build() override {
      return std::make_unique<DefaultScrollBehavior>();
    }
  };

  return std::make_unique<DefaultScrollBehaviorFactory>();
}

void EnterInsertMode(InsertModeOptions options) {
  EditorState* editor_state = options.editor_state;
  CHECK(editor_state != nullptr);

  if (!options.buffers.has_value()) {
    options.buffers = editor_state->active_buffers();
  }

  if (options.buffers.value().empty()) {
    options.buffers.value().push_back(OpenAnonymousBuffer(editor_state));
  }

  for (auto& buffer : options.buffers.value()) {
    auto target_buffer = buffer->GetBufferFromCurrentLine();
    if (target_buffer != nullptr) {
      buffer = target_buffer;
    }
  }

  if (!options.modify_handler) {
    options.modify_handler = [](const std::shared_ptr<OpenBuffer>&) {
      return futures::Past(true); /* Nothing. */
    };
  }

  if (options.scroll_behavior == nullptr) {
    options.scroll_behavior = ScrollBehaviorFactory::Default();
  }

  if (!options.escape_handler) {
    options.escape_handler = []() { /* Nothing. */ };
  }

  if (!options.new_line_handler) {
    options.new_line_handler = [](const std::shared_ptr<OpenBuffer>& buffer) {
      return buffer->ApplyToCursors(NewTransformation(
          Modifiers(), std::make_unique<NewLineTransformation>()));
    };
  }

  if (!options.start_completion) {
    options.start_completion = [](const std::shared_ptr<OpenBuffer>& buffer) {
      LOG(INFO) << "Start default completion.";
      buffer->ApplyToCursors(NewExpandTransformation());
      return true;
    };
  }

  options.editor_state->status()->Reset();
  for (auto& buffer : options.buffers.value()) {
    buffer->status()->Reset();
  }

  // TODO: Merge `RawInputTypeMode` back to normal insert mode, so that it works
  // better in multi buffers mode.
  auto first_buffer = options.buffers.value()[0];
  if (options.buffers.value().size() == 1 && first_buffer->fd() != nullptr) {
    first_buffer->status()->SetInformationText(L"🔡 (raw)");
    first_buffer->set_mode(std::make_unique<RawInputTypeMode>(first_buffer));
  } else if (editor_state->structure() == StructureChar() ||
             editor_state->structure() == StructureLine()) {
    for (auto& buffer : options.buffers.value()) {
      buffer->CheckPosition();
      buffer->PushTransformationStack();
      buffer->PushTransformationStack();
    }
    if (editor_state->structure() == StructureLine()) {
      for (auto& buffer : options.buffers.value()) {
        buffer->ApplyToCursors(NewTransformation(
            Modifiers(), std::make_unique<InsertEmptyLineTransformation>(
                             editor_state->direction())));
      }
    }
    EnterInsertCharactersMode(options);
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace afc::editor
