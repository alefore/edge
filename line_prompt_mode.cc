#include <memory>
#include <limits>
#include <string>

#include "char_buffer.h"
#include "command.h"
#include "command_mode.h"
#include "line_prompt_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "terminal.h"

namespace {
using namespace afc::editor;
using std::make_pair;
using std::numeric_limits;

const string kHistoryName = "- prompt history";

class LinePromptMode : public EditorMode {
 public:
  LinePromptMode(const string& prompt, LinePromptHandler handler)
      : prompt_(prompt),
        handler_(handler),
        input_(EditableString::New("")),
        history_position_(numeric_limits<size_t>::max()) {}

  static shared_ptr<OpenBuffer> FindHistoryBuffer(EditorState* editor_state) {
    auto result = editor_state->buffers()->find(kHistoryName);
    if (result == editor_state->buffers()->end()) {
      return shared_ptr<OpenBuffer>(nullptr);
    }
    return result->second;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        InsertToHistory(editor_state);
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_(input_->ToString(), editor_state);
        return;

      case Terminal::ESCAPE:
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_("", editor_state);
        return;

      case Terminal::BACKSPACE:
        input_->Backspace();
        break;

      case Terminal::UP_ARROW:
        {
          auto buffer = FindHistoryBuffer(editor_state);
          if (buffer == nullptr || buffer->contents()->empty()) { return; }
          if (history_position_ > 0) {
            history_position_ =
              min(history_position_, buffer->contents()->size()) - 1;
          }
          SetInputFromCurrentLine(buffer);
        }
        break;

      case Terminal::DOWN_ARROW:
        {
          auto buffer = FindHistoryBuffer(editor_state);
          if (buffer == nullptr || buffer->contents()->empty()) { return; }
          history_position_ =
              min(history_position_, buffer->contents()->size() - 1) + 1;
          if (history_position_ < buffer->contents()->size()) {
            SetInputFromCurrentLine(buffer);
          } else {
            input_ = EditableString::New("");
          }
        }
        break;

      default:
        input_->Insert(static_cast<char>(c));
    }
    editor_state->SetStatus(prompt_ + input_->ToString());
  }

 private:
  void SetInputFromCurrentLine(const shared_ptr<OpenBuffer>& buffer) {
    if (buffer == nullptr || buffer->contents()->empty()) {
      input_ = EditableString::New("");
      return;
    }
    auto line = buffer->contents()->at(history_position_)->contents;
    input_ = EditableString::New(line->ToString());
  }

  void InsertToHistory(EditorState* editor_state) {
    if (input_->size() == 0) { return; }
    auto insert_result = editor_state->buffers()->insert(
        make_pair(kHistoryName, nullptr));
    if (insert_result.second) {
      insert_result.first->second.reset(new OpenBuffer(kHistoryName));
      if (!editor_state->has_current_buffer()) {
        // Seems lame, but what can we do?
        editor_state->set_current_buffer(insert_result.first);
        editor_state->ScheduleRedraw();
      }
    }
    insert_result.first->second->AppendLine(input_);
  }

  const string prompt_;
  LinePromptHandler handler_;
  shared_ptr<EditableString> input_;
  size_t history_position_;
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
    editor_state->set_mode(std::unique_ptr<EditorMode>(
        new LinePromptMode(prompt_, handler_)));
    auto history = editor_state->buffers()->find(kHistoryName);
    if (history != editor_state->buffers()->end()
        && !history->second->contents()->empty()) {
      history->second->set_current_position_line(
          history->second->contents()->size() - 1);
    }
    editor_state->set_status_prompt(true);
    editor_state->SetStatus(prompt_);
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
