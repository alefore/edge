#include <memory>
#include <list>
#include <string>

#include "command.h"
#include "command_mode.h"
#include "line_prompt_mode.h"
#include "editor.h"
#include "terminal.h"

namespace {
using namespace afc::editor;

class LinePromptMode : public EditorMode {
 public:
  LinePromptMode(const string& prompt, LinePromptHandler handler)
      : prompt_(prompt), handler_(handler) {}

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        editor_state->status = "";
        handler_(input_, editor_state);
        return;

      case Terminal::ESCAPE:
        handler_("", editor_state);
        return;

      case Terminal::BACKSPACE:
        if (input_.empty()) {
          return;
        }
        input_.resize(input_.size() - 1);
        break;

      default:
        input_.push_back(static_cast<char>(c));
    }
    editor_state->status = prompt_ + input_;
  }

 private:
  const string prompt_;
  LinePromptHandler handler_;
  string input_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(const string& prompt,
                    const string& description,
                    LinePromptHandler handler)
      : prompt_(prompt), description_(description), handler_(handler) {}

  const string Description() {
    return description_;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->mode = std::unique_ptr<EditorMode>(
        new LinePromptMode(prompt_, handler_));
    editor_state->status = prompt_;
  }

 private:
  const string prompt_;
  const string description_;
  LinePromptHandler handler_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

unique_ptr<Command> NewLinePromptCommand(
    const string& prompt,
    const string& description,
    LinePromptHandler handler) {
  return std::move(unique_ptr<Command>(
      new LinePromptCommand(prompt, description, handler)));
}

}  // namespace afc
}  // namespace editor
