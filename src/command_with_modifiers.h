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
  using TransformationFactory = std::function<std::unique_ptr<Transformation>(
      EditorState*, OpenBuffer*, Argument)>;

  TransformationArgumentMode(wstring name, EditorState* editor_state,
                             Argument initial_value,
                             TransformationFactory transformation_factory)
      : name_(std::move(name)),
        buffer_(editor_state->current_buffer()),
        initial_value_(std::move(initial_value)),
        transformation_factory_(std::move(transformation_factory)) {
    CHECK(buffer_ != nullptr);
    Transform(editor_state, Transformation::Input::Mode::kPreview);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    buffer_->Undo(OpenBuffer::UndoMode::kOnlyOne)
        .SetConsumer([this, c, editor_state](bool) {
          switch (c) {
            case Terminal::BACKSPACE:
              if (!argument_string_.empty()) {
                argument_string_.pop_back();
              }
              Transform(editor_state, Transformation::Input::Mode::kPreview);
              break;
            default:
              Argument dummy;
              if (!TransformationArgumentApplyChar(c, &dummy)) {
                if (static_cast<int>(c) != Terminal::ESCAPE) {
                  Transform(editor_state, Transformation::Input::Mode::kFinal);
                }
                buffer_->ResetMode();
                buffer_->status()->Reset();
                editor_state->status()->Reset();
                if (c != L'\n') {
                  editor_state->ProcessInput(c);
                }
              } else {
                argument_string_.push_back(c);
                Transform(editor_state, Transformation::Input::Mode::kPreview);
              }
          }
        });
  }

 private:
  void Transform(EditorState* editor_state,
                 Transformation::Input::Mode apply_mode) {
    auto argument = initial_value_;
    for (const auto& c : argument_string_) {
      TransformationArgumentApplyChar(c, &argument);
    }
    buffer_->status()->SetInformationText(
        TransformationArgumentBuildStatus(argument, name_));
    auto cursors_affected = TransformationArgumentCursorsAffected(argument);
    buffer_->ApplyToCursors(transformation_factory_(editor_state, buffer_.get(),
                                                    std::move(argument)),
                            cursors_affected, apply_mode);
  }

  const wstring name_;
  const std::shared_ptr<OpenBuffer> buffer_;
  const Argument initial_value_;
  const TransformationFactory transformation_factory_;
  wstring argument_string_;
};

using CommandWithModifiersHandler =
    std::function<std::unique_ptr<Transformation>(EditorState*, OpenBuffer*,
                                                  Modifiers)>;

std::unique_ptr<Command> NewCommandWithModifiers(
    wstring name, wstring description, CommandWithModifiersHandler handler);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
