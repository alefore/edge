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
//   // Returns true if the character was accepted.
//   bool TransformationArgumentApplyChar(wint_t c, Argument* output_argument);
//
//   // Returns the string to show in the status.
//   std::wstring TransformationArgumentBuildStatus(
//       const Argument& argument, std::wstring name);
//
//   // Returns the mode in which the transformation should be applied.
//   Modifiers::CursorsAffected TransformationArgumentCursorsAffected(
//       const Argument& argument);
template <typename Argument>
class TransformationArgumentMode : public EditorMode {
 public:
  using TransformationFactory =
      std::function<std::unique_ptr<Transformation>(EditorState*, Argument)>;

  TransformationArgumentMode(
      wstring name, EditorState* editor_state,
      std::function<Argument(const std::shared_ptr<OpenBuffer>&)>
          initial_value_factory,
      TransformationFactory transformation_factory)
      : name_(std::move(name)),
        buffers_(editor_state->active_buffers()),
        initial_value_factory_(std::move(initial_value_factory)),
        transformation_factory_(std::move(transformation_factory)) {
    Transform(editor_state, Transformation::Input::Mode::kPreview);
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
              return Transform(editor_state,
                               Transformation::Input::Mode::kPreview);
            default:
              Argument dummy;
              if (TransformationArgumentApplyChar(c, &dummy)) {
                argument_string_.push_back(c);
                return Transform(editor_state,
                                 Transformation::Input::Mode::kPreview);
              }
              return futures::Transform(
                  static_cast<int>(c) == Terminal::ESCAPE
                      ? futures::Past(true)
                      : Transform(editor_state,
                                  Transformation::Input::Mode::kFinal),
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
  futures::Value<bool> ForEachBuffer(
      const std::function<futures::Value<futures::IterationControlCommand>(
          const std::shared_ptr<OpenBuffer>&)>& callback) {
    return futures::ImmediateTransform(
        futures::ForEach(buffers_.begin(), buffers_.end(), callback),
        [](futures::IterationControlCommand) { return true; });
  }

  futures::Value<bool> Transform(EditorState* editor_state,
                                 Transformation::Input::Mode apply_mode) {
    return ForEachBuffer([transformation_factory = transformation_factory_,
                          initial_value_factory = initial_value_factory_,
                          argument_string = argument_string_, name = name_,
                          editor_state, apply_mode](
                             const std::shared_ptr<OpenBuffer>& buffer) {
      auto argument = initial_value_factory(buffer);
      for (const auto& c : argument_string) {
        TransformationArgumentApplyChar(c, &argument);
      }

      buffer->status()->SetInformationText(
          TransformationArgumentBuildStatus(argument, name));
      auto cursors_affected = TransformationArgumentCursorsAffected(argument);
      return futures::ImmediateTransform(
          buffer->ApplyToCursors(
              transformation_factory(editor_state, std::move(argument)),
              cursors_affected, apply_mode),
          [](bool) { return futures::IterationControlCommand::kContinue; });
    });
  }

  const wstring name_;
  const std::vector<std::shared_ptr<OpenBuffer>> buffers_;
  const std::function<Argument(const std::shared_ptr<OpenBuffer>&)>
      initial_value_factory_;
  const TransformationFactory transformation_factory_;
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
