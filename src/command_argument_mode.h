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
    EditorState& editor_state;
    Argument initial_value = Argument();

    std::function<bool(wint_t, Argument&)> char_consumer;

    // Returns the string to show in the status.
    std::function<std::wstring(const Argument&)> status_factory;

    std::function<futures::Value<language::EmptyValue>()> undo = nullptr;
    std::function<futures::Value<language::EmptyValue>(
        CommandArgumentModeApplyMode, Argument)>
        apply = nullptr;
  };

  CommandArgumentMode(Options options)
      : options_(options), buffers_(options_.editor_state.active_buffers()) {
    CHECK(options_.char_consumer != nullptr);
    CHECK(options_.status_factory != nullptr);
    CHECK(options_.undo != nullptr);
    CHECK(options_.apply != nullptr);
    Transform(CommandArgumentModeApplyMode::kPreview, BuildArgument());
  }

  void ProcessInput(wint_t c) override {
    options_.undo().Transform([this, c](language::EmptyValue) {
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
          if (ApplyChar(c, argument)) {
            argument_string_.push_back(c);
            return Transform(CommandArgumentModeApplyMode::kPreview, argument);
          }
          return (static_cast<int>(c) == Terminal::ESCAPE
                      ? futures::Past(language::EmptyValue())
                      : Transform(CommandArgumentModeApplyMode::kFinal,
                                  argument))
              .Transform([&editor_state = options_.editor_state,
                          c](language::EmptyValue) {
                editor_state.status().Reset();
                auto& editor_state_copy = editor_state;
                editor_state.set_keyboard_redirect(nullptr);
                if (c != L'\n') {
                  editor_state_copy.ProcessInput(c);
                }
                return language::EmptyValue();
              });
      }
    });
  }

  CursorMode cursor_mode() const override { return CursorMode::kDefault; }

 private:
  Argument BuildArgument() {
    auto argument = options_.initial_value;
    for (const auto& c : argument_string_) {
      CHECK(ApplyChar(c, argument));
    }
    return argument;
  }

  bool ApplyChar(wint_t c, Argument& argument) {
    return options_.char_consumer(c, argument);
  }

  futures::Value<language::EmptyValue> Transform(
      CommandArgumentModeApplyMode apply_mode, Argument argument) {
    options_.editor_state.status().SetInformationText(
        options_.status_factory(argument));
    return options_.apply(apply_mode, std::move(argument));
  }

  const Options options_;
  const std::vector<language::gc::Root<OpenBuffer>> buffers_;
  std::wstring argument_string_;
};

// Sets parameter `undo` and `apply`. All other parameters must already have
// been set.
template <typename Argument>
void SetOptionsForBufferTransformation(
    std::function<transformation::Variant(Argument)> transformation_factory,
    std::function<std::optional<Modifiers::CursorsAffected>(const Argument&)>
        cursors_affected_factory,
    typename CommandArgumentMode<Argument>::Options& options) {
  namespace gc = afc::language::gc;
  auto buffers = std::make_shared<std::vector<gc::Root<OpenBuffer>>>(
      options.editor_state.active_buffers());
  auto for_each_buffer =
      [buffers](
          const std::function<futures::Value<futures::IterationControlCommand>(
              const gc::Root<OpenBuffer>&)>& callback) {
        return futures::ForEach(buffers->begin(), buffers->end(), callback)
            .Transform([buffers](futures::IterationControlCommand) {
              return language::EmptyValue();
            });
      };

  options.undo = [for_each_buffer] {
    return for_each_buffer([](const gc::Root<OpenBuffer>& buffer) {
      return buffer.ptr()
          ->Undo(OpenBuffer::UndoMode::kOnlyOne)
          .Transform([](language::EmptyValue) {
            return futures::IterationControlCommand::kContinue;
          });
    });
  };
  options.apply = [transformation_factory, cursors_affected_factory,
                   for_each_buffer](CommandArgumentModeApplyMode mode,
                                    Argument argument) {
    return for_each_buffer(
        [transformation_factory, cursors_affected_factory, mode,
         argument = std::move(argument)](const gc::Root<OpenBuffer>& buffer) {
          auto cursors_affected = cursors_affected_factory(argument).value_or(
              buffer.ptr()->Read(buffer_variables::multiple_cursors)
                  ? Modifiers::CursorsAffected::kAll
                  : Modifiers::CursorsAffected::kOnlyCurrent);
          return buffer.ptr()
              ->ApplyToCursors(transformation_factory(std::move(argument)),
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
