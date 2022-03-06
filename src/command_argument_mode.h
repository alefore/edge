#ifndef __AFC_EDITOR_COMMAND_ARGUMENT_MODE_H__
#define __AFC_EDITOR_COMMAND_ARGUMENT_MODE_H__

#include <memory>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/terminal.h"

namespace afc::editor {

enum class CommandArgumentModeApplyMode {
  // We're only updating the state to preview what the result of the operation
  // would be.
  kPreview,
  // We're actually executing the command.
  kFinal
};

// General mode that collects characters and uses the to modify an argument of
// an arbitrary type. When ENTER is pressed, the transformation is finally
// executed and the mode is reset.
//
// Every time the argument is modified, the transformation is executed, just in
// kPreview mode.
template <typename Argument>
class CommandArgumentMode : public EditorMode {
 public:
  struct Options {
    EditorState* editor_state;
    Argument initial_value = Argument();

    std::function<bool(wint_t, Argument*)> char_consumer;

    // Returns the string to show in the status.
    std::function<std::wstring(const Argument&)> status_factory;

    std::function<futures::Value<EmptyValue>()> undo = nullptr;
    std::function<futures::Value<EmptyValue>(CommandArgumentModeApplyMode,
                                             Argument)>
        apply = nullptr;
  };

  CommandArgumentMode(Options options)
      : options_(options), buffers_(options_.editor_state->active_buffers()) {
    CHECK(options_.editor_state != nullptr);
    CHECK(options_.char_consumer != nullptr);
    CHECK(options_.status_factory != nullptr);
    CHECK(options_.undo != nullptr);
    CHECK(options_.apply != nullptr);
    Transform(CommandArgumentModeApplyMode::kPreview, BuildArgument());
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    options_.undo().Transform([this, c, editor_state](EmptyValue) {
      // TODO: Get rid of this cast, ugh.
      switch (static_cast<int>(c)) {
        case Terminal::BACKSPACE:
          if (!argument_string_.empty()) {
            argument_string_.pop_back();
          }
          return Transform(CommandArgumentModeApplyMode::kPreview,
                           BuildArgument());
        default:
          auto argument = BuildArgument();
          if (ApplyChar(c, &argument)) {
            argument_string_.push_back(c);
            return Transform(CommandArgumentModeApplyMode::kPreview, argument);
          }
          return (static_cast<int>(c) == Terminal::ESCAPE
                      ? futures::Past(EmptyValue())
                      : Transform(CommandArgumentModeApplyMode::kFinal,
                                  argument))
              .Transform([editor_state, c](EmptyValue) {
                editor_state->status()->Reset();
                auto editor_state_copy = editor_state;
                editor_state->set_keyboard_redirect(nullptr);
                if (c != L'\n') {
                  editor_state_copy->ProcessInput(c);
                }
                return EmptyValue();
              });
      }
    });
  }

  CursorMode cursor_mode() const override { return CursorMode::kDefault; }

 private:
  Argument BuildArgument() {
    auto argument = options_.initial_value;
    for (const auto& c : argument_string_) {
      CHECK(ApplyChar(c, &argument));
    }
    return argument;
  }

  bool ApplyChar(wint_t c, Argument* argument) {
    return options_.char_consumer(c, argument);
  }

  futures::Value<EmptyValue> Transform(CommandArgumentModeApplyMode apply_mode,
                                       Argument argument) {
    options_.editor_state->status()->SetInformationText(
        options_.status_factory(argument));
    return options_.apply(apply_mode, std::move(argument));
  }

  const Options options_;
  const std::vector<std::shared_ptr<OpenBuffer>> buffers_;
  wstring argument_string_;
};

// Sets parameter `undo` and `apply`. All other parameters must already have
// been set.
template <typename Argument>
void SetOptionsForBufferTransformation(
    std::function<transformation::Variant(EditorState*, Argument)>
        transformation_factory,
    std::function<std::optional<Modifiers::CursorsAffected>(const Argument&)>
        cursors_affected_factory,
    typename CommandArgumentMode<Argument>::Options* options) {
  CHECK(options != nullptr);
  CHECK(options->editor_state != nullptr);
  auto buffers = std::make_shared<std::vector<std::shared_ptr<OpenBuffer>>>(
      options->editor_state->active_buffers());
  auto for_each_buffer =
      [buffers](
          const std::function<futures::Value<futures::IterationControlCommand>(
              const std::shared_ptr<OpenBuffer>&)>& callback) {
        return futures::ForEach(buffers->begin(), buffers->end(), callback)
            .Transform([buffers](futures::IterationControlCommand) {
              return EmptyValue();
            });
      };

  options->undo = [editor_state = options->editor_state, for_each_buffer] {
    return for_each_buffer([](const std::shared_ptr<OpenBuffer>& buffer) {
      return buffer->Undo(OpenBuffer::UndoMode::kOnlyOne)
          .Transform([](EmptyValue) {
            return futures::IterationControlCommand::kContinue;
          });
    });
  };
  options->apply = [editor_state = options->editor_state,
                    transformation_factory, cursors_affected_factory,
                    for_each_buffer](CommandArgumentModeApplyMode mode,
                                     Argument argument) {
    return for_each_buffer(
        [editor_state, transformation_factory, cursors_affected_factory, mode,
         argument =
             std::move(argument)](const std::shared_ptr<OpenBuffer>& buffer) {
          auto cursors_affected = cursors_affected_factory(argument).value_or(
              buffer->Read(buffer_variables::multiple_cursors)
                  ? Modifiers::CursorsAffected::kAll
                  : Modifiers::CursorsAffected::kOnlyCurrent);
          return buffer
              ->ApplyToCursors(
                  transformation_factory(editor_state, std::move(argument)),
                  cursors_affected,
                  mode == CommandArgumentModeApplyMode::kPreview
                      ? transformation::Input::Mode::kPreview
                      : transformation::Input::Mode::kFinal)
              .Transform([](auto) {
                return futures::IterationControlCommand::kContinue;
              });
        });
  };
}

}  // namespace afc::editor

#endif  // __AFC_EDITOR_COMMAND_ARGUMENT_MODE_H__
