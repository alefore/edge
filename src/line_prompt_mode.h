#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <functional>
#include <memory>
#include <string>

#include "src/command.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/predictor.h"

namespace afc::editor {

GHOST_TYPE(HistoryFile, std::wstring);

HistoryFile HistoryFileFiles();
HistoryFile HistoryFileCommands();

struct TokenAndModifiers {
  // The portion to colorize. The `value` field is ignored; instead, the
  // corresponding portion from the value in `prompt` will be used.
  language::lazy_string::Token token;
  // Set of modifiers to apply.
  infrastructure::screen::LineModifierSet modifiers;
};

struct ColorizePromptOptions {
  std::vector<TokenAndModifiers> tokens = {};

  // Leaves the context unmodified;
  struct ContextUnmodified {};
  // Clears the context (if it was set to a specific buffer).
  struct ContextClear {};
  // Sets the context to a specific buffer.
  struct ContextBuffer {
    language::gc::Root<OpenBuffer> buffer;
  };

  std::variant<ContextUnmodified, ContextClear, ContextBuffer> context =
      ContextUnmodified{};
};

struct PromptOptions {
  EditorState& editor_state;

  // Text to show in the prompt.
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> prompt;

  // Used to set buffer_variables::contents_type on the buffer for the prompt.
  // The extensions code inspects this and can adjust behaviors.
  std::wstring prompt_contents_type = L"";

  // Name of the file with the history for this type of prompt.
  HistoryFile history_file;

  // Optional. Initial value for the prompt. Defaults to empty.
  std::wstring initial_value = L"";

  using ColorizeFunction = std::function<futures::Value<ColorizePromptOptions>(
      const language::NonNull<
          std::shared_ptr<language::lazy_string::LazyString>>& line,
      language::NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
      futures::DeleteNotification::Value abort_value)>;

  // Run whenever the text in the promot changes; should return a future with
  // options to colorize it.
  ColorizeFunction colorize_options_provider = nullptr;

  // Function to run when the prompt receives the final input.
  std::function<futures::Value<language::EmptyValue>(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>)>
      handler;

  // Optional. Function to run when the prompt is cancelled (because ESCAPE was
  // pressed). If empty, handler will be run with an empty input.
  std::function<void()> cancel_handler = nullptr;

  // Optional. Useful for automatic completion.
  Predictor predictor = EmptyPredictor;

  // Source buffers to give to the predictor. See
  // `PredictorInput::source_buffers`.
  std::vector<language::gc::Root<OpenBuffer>> source_buffers = {};

  enum class Status { kEditor, kBuffer };
  Status status = Status::kEditor;
};

void AddLineToHistory(
    EditorState& editor, const HistoryFile& history_file,
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        input);

void Prompt(PromptOptions options);

// options_supplier will only be called if the editor has an active buffer.
language::gc::Root<Command> NewLinePromptCommand(
    EditorState& editor_state, std::wstring description,
    std::function<PromptOptions()> options_supplier);

}  // namespace afc::editor

#endif
