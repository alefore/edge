#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/predictor.h"
#include "src/tokenize.h"

namespace afc {
namespace editor {

using std::unique_ptr;

struct TokenAndModifiers {
  // The portion to colorize. The `value` field is ignored; instead, the
  // corresponding portion from the value in `prompt` will be used.
  Token token;
  // Set of modifiers to apply.
  LineModifierSet modifiers;
};

struct ColorizePromptOptions {
  std::vector<TokenAndModifiers> tokens = {};

  // If present, sets the context buffer for the prompt. Can be `nullptr` (which
  // will clear any previously set context).
  std::optional<std::shared_ptr<OpenBuffer>> context = std::nullopt;
};

struct PromptOptions {
  // TODO(easy): Make this a reference.
  EditorState* editor_state = nullptr;

  // Text to show in the prompt.
  wstring prompt;

  // Used to set buffer_variables::contents_type on the buffer for the prompt.
  // The extensions code inspects this and can adjust behaviors.
  std::wstring prompt_contents_type = L"";

  // Optional. Name of the file with the history for this type of prompt.
  // Defaults to no history.
  wstring history_file = L"";

  // Optional. Initial value for the prompt. Defaults to empty.
  wstring initial_value = L"";

  // Run whenever the text in the promot changes; should return a future with
  // options to colorize it.
  std::function<futures::Value<ColorizePromptOptions>(
      const std::shared_ptr<LazyString>& line,
      std::unique_ptr<ProgressChannel> progress_channel,
      std::shared_ptr<Notification> abort_notification)>
      colorize_options_provider = nullptr;

  // Function to run when the prompt receives the final input.
  std::function<futures::Value<EmptyValue>(const wstring& input,
                                           EditorState* editor)>
      handler;

  // Optional. Function to run when the prompt is cancelled (because ESCAPE was
  // pressed). If empty, handler will be run with an empty input.
  std::function<void(EditorState* editor)> cancel_handler = nullptr;

  // Optional. Useful for automatic completion.
  Predictor predictor = EmptyPredictor;

  // Source buffers to give to the predictor. See
  // `PredictorInput::source_buffers`.
  std::vector<std::shared_ptr<OpenBuffer>> source_buffers = {};

  enum class Status { kEditor, kBuffer };
  Status status = Status::kEditor;
};

void Prompt(PromptOptions options);

// options_supplier will only be called if the editor has an active buffer.
unique_ptr<Command> NewLinePromptCommand(
    EditorState& editor_state, wstring description,
    std::function<PromptOptions(EditorState*)> options_supplier);

}  // namespace editor
}  // namespace afc

#endif
