#include "line_prompt_mode.h"

#include <memory>
#include <limits>
#include <string>

#include "buffer.h"
#include "char_buffer.h"
#include "command.h"
#include "command_mode.h"
#include "file_link_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "predictor.h"
#include "terminal.h"

namespace {

using namespace afc::editor;
using std::make_pair;
using std::numeric_limits;

map<string, shared_ptr<OpenBuffer>>::iterator
GetHistoryBuffer(EditorState* editor_state, const string& name) {
  const string actual_name = "- history: " + name;
  auto it = editor_state->buffers()->find(actual_name);
  if (it != editor_state->buffers()->end()) {
    return it;
  }
  it = OpenFile(
      editor_state, actual_name,
      (*editor_state->edge_path().rbegin()) + "/" + name + "_history");
  assert(it != editor_state->buffers()->end());
  assert(it->second != nullptr);
  it->second->set_bool_variable(
      OpenBuffer::variable_save_on_close(), true);
  if (!editor_state->has_current_buffer()) {
    // Seems lame, but what can we do?
    editor_state->set_current_buffer(it);
    editor_state->ScheduleRedraw();
  }
  return it;
}

map<string, shared_ptr<OpenBuffer>>::iterator
GetPredictionsBuffer(
    EditorState* editor_state,
    const Predictor& predictor,
    const string& input,
    function<void(const string&)> consumer) {
  auto it = editor_state->buffers()->insert(make_pair("- predictions", nullptr));
  it.first->second = PredictionsBuffer(predictor, input, consumer);
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  it.first->second->set_current_position_col(0);
  return it.first;
}

class LinePromptMode : public EditorMode {
 public:
  LinePromptMode(const string& prompt, const string& history_file,
                 const string& initial_value, LinePromptHandler handler,
                 Predictor predictor)
      : prompt_(prompt),
        history_file_(history_file),
        handler_(handler),
        predictor_(predictor),
        input_(EditableString::New(initial_value)) {}

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        if (input_->size() != 0) {
          GetHistoryBuffer(editor_state, history_file_)
              ->second->AppendLine(input_);
        }
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

      case '\t':
        {
          GetPredictionsBuffer(
              editor_state, predictor_, input_->ToString(),
              [editor_state, this](const string& prediction) {
                input_ = EditableString::New(prediction);
                UpdateStatus(editor_state);
              });
        }
        break;

      case Terminal::CTRL_U:
        input_->Clear();
        break;

      case Terminal::UP_ARROW:
        {
          auto buffer = GetHistoryBuffer(editor_state, history_file_)->second;
          if (buffer == nullptr || buffer->contents()->size() == 1) { return; }
          OpenBuffer::Position position = buffer->position();
          if (position.line > 0) {
            position.line --;
            buffer->set_position(position);
          }
          SetInputFromCurrentLine(buffer);
        }
        break;

      case Terminal::DOWN_ARROW:
        {
          auto buffer = GetHistoryBuffer(editor_state, history_file_)->second;
          if (buffer == nullptr || buffer->contents()->size() == 1) { return; }
          OpenBuffer::Position position = buffer->position();
          if (position.line + 1 < buffer->contents()->size()) {
            position.line ++;
            buffer->set_position(position);
          }
          SetInputFromCurrentLine(buffer);
        }
        break;

      default:
        input_->Insert(static_cast<char>(c));
    }
    UpdateStatus(editor_state);
  }

  void UpdateStatus(EditorState* editor_state) {
    editor_state->SetStatus(prompt_ + input_->ToString());
  }

 private:
  void SetInputFromCurrentLine(const shared_ptr<OpenBuffer>& buffer) {
    if (buffer == nullptr) {
      input_ = EditableString::New("");
      return;
    }
    input_ = EditableString::New(buffer->current_line()->contents->ToString());
  }

  const string prompt_;
  // Name of the file in which the history for this prompt should be preserved.
  const string history_file_;
  LinePromptHandler handler_;
  Predictor predictor_;
  shared_ptr<EditableString> input_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(const string& prompt,
                    const string& history_file,
                    const string& description,
                    LinePromptHandler handler,
                    Predictor predictor)
      : prompt_(prompt),
        history_file_(history_file),
        description_(description),
        handler_(handler),
        predictor_(predictor) {}

  const string Description() {
    return description_;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Prompt(editor_state, prompt_, history_file_, "", handler_, predictor_);
  }

 private:
  const string prompt_;
  const string history_file_;
  const string description_;
  LinePromptHandler handler_;
  Predictor predictor_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void Prompt(EditorState* editor_state,
            const string& prompt,
            const string& history_file,
            const string& initial_value,
            LinePromptHandler handler,
            Predictor predictor) {
  std::unique_ptr<LinePromptMode> line_prompt_mode(new LinePromptMode(
      prompt, history_file, initial_value, handler, predictor));
  auto history = GetHistoryBuffer(editor_state, history_file);
  history->second->set_current_position_line(
      history->second->contents()->size() - 1);
  line_prompt_mode->UpdateStatus(editor_state);
  editor_state->set_mode(std::move(line_prompt_mode));
  editor_state->set_status_prompt(true);
}

unique_ptr<Command> NewLinePromptCommand(
    const string& prompt,
    const string& history_file,
    const string& description,
    LinePromptHandler handler,
    Predictor predictor) {
  return std::move(unique_ptr<Command>(new LinePromptCommand(
      prompt, history_file, description, handler, predictor)));
}

}  // namespace afc
}  // namespace editor
