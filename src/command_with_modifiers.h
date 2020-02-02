#ifndef __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
#define __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__

#include <memory>

#include "src/buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/terminal.h"

namespace afc {
namespace editor {

enum class CommandApplyMode {
  // Just preview what this transformation would do. Don't apply any
  // long-lasting effects.
  PREVIEW,
  // Apply the transformation.
  FINAL,
};

// General mode that collects characters and uses the to modify an argument of
// an arbitrary type. When ENTER is pressed, the transformation is finally
// executed and the mode is reset.
//
// Every time the argument is modified, the transformation is actually ... also
// executed, just in kPreview mode.
//
// This requires the following symbols to be defined:
//
//   // Returns the mode in which the transformation should be applied.
//   Modifiers::CursorsAffected TransformationArgumentCursorsAffected(
//       const Argument& argument);
template <typename Argument>
class TransformationArgumentMode : public EditorMode {
 public:
  using TransformationFactory =
      std::function<std::unique_ptr<Transformation>(EditorState*, Argument)>;

  struct CharHandler {
    std::function<Argument(Argument)> apply;
  };

  struct Options {
    EditorState* editor_state;
    std::function<Argument(const std::shared_ptr<OpenBuffer>&)>
        initial_value_factory;
    TransformationFactory transformation_factory;
    std::shared_ptr<const std::unordered_map<wint_t, CharHandler>> characters;
    // Returns the string to show in the status.
    std::function<std::wstring(const Argument&)> status_factory;
  };

  TransformationArgumentMode(Options options)
      : options_(options), buffers_(options_.editor_state->active_buffers()) {
    Transform(Transformation::Input::Mode::kPreview);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    futures::Transform(
        ForEachBuffer([](const std::shared_ptr<OpenBuffer>& buffer) {
          return futures::ImmediateTransform(
              buffer->Undo(OpenBuffer::UndoMode::kOnlyOne),
              [](bool) { return futures::IterationControlCommand::kContinue; });
        }),
        [this, c, editor_state](bool) {
          switch (c) {
            case Terminal::BACKSPACE:
              if (!argument_string_.empty()) {
                argument_string_.pop_back();
              }
              return Transform(Transformation::Input::Mode::kPreview);
            default:
              Argument dummy;
              if (ApplyChar(options_, c, &dummy)) {
                argument_string_.push_back(c);
                return Transform(Transformation::Input::Mode::kPreview);
              }
              return futures::Transform(
                  static_cast<int>(c) == Terminal::ESCAPE
                      ? futures::Past(true)
                      : Transform(Transformation::Input::Mode::kFinal),
                  [this, editor_state](bool) {
                    return ForEachBuffer(
                        [editor_state](
                            const std::shared_ptr<OpenBuffer>& buffer) {
                          buffer->status()->Reset();
                          editor_state->status()->Reset();
                          return futures::Past(
                              futures::IterationControlCommand::kContinue);
                        });
                  },
                  [editor_state, c](bool) {
                    auto editor_state_copy = editor_state;
                    editor_state->set_keyboard_redirect(nullptr);
                    if (c != L'\n') {
                      editor_state_copy->ProcessInput(c);
                    }
                    return futures::Past(true);
                  });
          }
        });
  }

 private:
  static bool ApplyChar(Options options, wint_t c, Argument* argument) {
    auto it = options.characters->find(c);
    if (it == options.characters->end()) return false;
    *argument = it->second.apply(std::move(*argument));
    return true;
  }

  futures::Value<bool> ForEachBuffer(
      const std::function<futures::Value<futures::IterationControlCommand>(
          const std::shared_ptr<OpenBuffer>&)>& callback) {
    return futures::ImmediateTransform(
        futures::ForEach(buffers_.begin(), buffers_.end(), callback),
        [](futures::IterationControlCommand) { return true; });
  }

  futures::Value<bool> Transform(Transformation::Input::Mode apply_mode) {
    return ForEachBuffer([options = options_,
                          argument_string = argument_string_, apply_mode](
                             const std::shared_ptr<OpenBuffer>& buffer) {
      auto argument = options.initial_value_factory(buffer);
      for (const auto& c : argument_string) {
        ApplyChar(options, c, &argument);
      }

      buffer->status()->SetInformationText(options.status_factory(argument));
      auto cursors_affected = TransformationArgumentCursorsAffected(argument);
      return futures::ImmediateTransform(
          buffer->ApplyToCursors(options.transformation_factory(
                                     options.editor_state, std::move(argument)),
                                 cursors_affected, apply_mode),
          [](bool) { return futures::IterationControlCommand::kContinue; });
    });
  }

  const Options options_;
  const std::vector<std::shared_ptr<OpenBuffer>> buffers_;
  wstring argument_string_;
};

using CommandWithModifiersHandler =
    std::function<std::unique_ptr<Transformation>(EditorState*, Modifiers)>;

std::unique_ptr<Command> NewCommandWithModifiers(
    wstring name, wstring description, Modifiers initial_modifiers,
    CommandWithModifiersHandler handler);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
