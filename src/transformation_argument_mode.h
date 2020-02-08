#ifndef __AFC_EDITOR_TRANSFORMATION_ARGUMENT_MODE_H__
#define __AFC_EDITOR_TRANSFORMATION_ARGUMENT_MODE_H__

#include <memory>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/terminal.h"

namespace afc::editor {

// General mode that collects characters and uses the to modify an argument of
// an arbitrary type. When ENTER is pressed, the transformation is finally
// executed and the mode is reset.
//
// Every time the argument is modified, the transformation is executed, just in
// kPreview mode.
template <typename Argument>
class TransformationArgumentMode : public EditorMode {
 public:
  struct Options {
    EditorState* editor_state;
    Argument initial_value = Argument();

    std::function<bool(wint_t, Argument*)> char_consumer;

    // Returns the string to show in the status.
    std::function<std::wstring(const Argument&)> status_factory;

    std::function<futures::Value<bool>()> undo = nullptr;
    std::function<futures::Value<bool>(Transformation::Input::Mode, Argument)>
        apply = nullptr;
  };

  TransformationArgumentMode(Options options)
      : options_(options), buffers_(options_.editor_state->active_buffers()) {
    CHECK(options_.editor_state != nullptr);
    CHECK(options_.char_consumer != nullptr);
    CHECK(options_.status_factory != nullptr);
    CHECK(options_.undo != nullptr);
    CHECK(options_.apply != nullptr);
    Transform(Transformation::Input::Mode::kPreview, BuildArgument());
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    futures::Transform(options_.undo(), [this, c, editor_state](bool) {
      switch (c) {
        case Terminal::BACKSPACE:
          if (!argument_string_.empty()) {
            argument_string_.pop_back();
          }
          return Transform(Transformation::Input::Mode::kPreview,
                           BuildArgument());
        default:
          auto argument = BuildArgument();
          if (ApplyChar(c, &argument)) {
            argument_string_.push_back(c);
            return Transform(Transformation::Input::Mode::kPreview, argument);
          }
          return futures::Transform(
              static_cast<int>(c) == Terminal::ESCAPE
                  ? futures::Past(true)
                  : Transform(Transformation::Input::Mode::kFinal, argument),
              [editor_state, c](bool) {
                editor_state->status()->Reset();
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

  futures::Value<bool> Transform(Transformation::Input::Mode apply_mode,
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
    std::function<std::unique_ptr<Transformation>(EditorState*, Argument)>
        transformation_factory,
    std::function<std::optional<Modifiers::CursorsAffected>(const Argument&)>
        cursors_affected_factory,
    typename TransformationArgumentMode<Argument>::Options* options) {
  CHECK(options != nullptr);
  CHECK(options->editor_state != nullptr);
  auto buffers = std::make_shared<std::vector<std::shared_ptr<OpenBuffer>>>(
      options->editor_state->active_buffers());
  auto for_each_buffer =
      [buffers](
          const std::function<futures::Value<futures::IterationControlCommand>(
              const std::shared_ptr<OpenBuffer>&)>& callback) {
        return futures::ImmediateTransform(
            futures::ForEach(buffers->begin(), buffers->end(), callback),
            [buffers](futures::IterationControlCommand) { return true; });
      };

  options->undo = [editor_state = options->editor_state, for_each_buffer] {
    return for_each_buffer([](const std::shared_ptr<OpenBuffer>& buffer) {
      return futures::ImmediateTransform(
          buffer->Undo(OpenBuffer::UndoMode::kOnlyOne),
          [](bool) { return futures::IterationControlCommand::kContinue; });
    });
  };
  options->apply = [editor_state = options->editor_state,
                    transformation_factory, cursors_affected_factory,
                    for_each_buffer](Transformation::Input::Mode mode,
                                     Argument argument) {
    return for_each_buffer(
        [editor_state, transformation_factory, cursors_affected_factory, mode,
         argument =
             std::move(argument)](const std::shared_ptr<OpenBuffer>& buffer) {
          auto cursors_affected = cursors_affected_factory(argument).value_or(
              buffer->Read(buffer_variables::multiple_cursors)
                  ? Modifiers::CursorsAffected::kAll
                  : Modifiers::CursorsAffected::kOnlyCurrent);
          return futures::Transform(
              buffer->ApplyToCursors(
                  transformation_factory(editor_state, std::move(argument)),
                  cursors_affected, mode),
              futures::Past(futures::IterationControlCommand::kContinue));
        });
  };
}

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_ARGUMENT_MODE_H__
