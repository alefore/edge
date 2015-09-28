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
#include "wstring.h"

namespace afc {
namespace editor {
namespace {

using std::make_pair;
using std::numeric_limits;

const wstring kPredictionsBufferName = L"- predictions";

map<wstring, shared_ptr<OpenBuffer>>::iterator
GetHistoryBuffer(EditorState* editor_state, const wstring& name) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- history: " + name;
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    return it;
  }
  options.path =
      (*editor_state->edge_path().begin()) + L"/" + name + L"_history";
  options.make_current_buffer = false;
  it = OpenFile(options);
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

map<wstring, shared_ptr<OpenBuffer>>::iterator
GetPredictionsBuffer(
    EditorState* editor_state,
    const Predictor& predictor,
    const wstring& input,
    function<void(const wstring&)> consumer) {
  auto it = editor_state->buffers()
      ->insert(make_pair(kPredictionsBufferName, nullptr));
  it.first->second =
      PredictionsBuffer(editor_state, predictor, input, consumer);
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  it.first->second->set_current_position_col(0);
  return it.first;
}

class LinePromptMode : public EditorMode {
 public:
  LinePromptMode(const wstring& prompt, const wstring& history_file,
                 const wstring& initial_value, LinePromptHandler handler,
                 Predictor predictor,
                 map<wstring, shared_ptr<OpenBuffer>>::iterator initial_buffer)
      : prompt_(prompt),
        history_file_(history_file),
        handler_(handler),
        predictor_(predictor),
        initial_buffer_(initial_buffer),
        input_(EditableString::New(initial_value)),
        most_recent_char_(0) {}

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_status_prompt(true);
    if (initial_buffer_ != editor_state->current_buffer()
        && initial_buffer_ != editor_state->buffers()->end()) {
      editor_state->set_current_buffer(initial_buffer_);
      editor_state->ScheduleRedraw();
    }
    int last_char = most_recent_char_;
    most_recent_char_ = c;
    switch (c) {
      case '\n':
        if (input_->size() != 0) {
          auto history = GetHistoryBuffer(editor_state, history_file_)->second;
          assert(history != nullptr);
          if (history->contents()->size() == 0
              || (history->contents()->at(history->contents()->size() - 1)
                      ->ToString()
                  != input_->ToString())) {
            history->AppendLine(editor_state, input_);
          }
        }
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_(input_->ToString(), editor_state);
        return;

      case Terminal::ESCAPE:
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        handler_(L"", editor_state);
        return;

      case Terminal::BACKSPACE:
        input_->Backspace();
        break;

      case '\t':
        if (last_char == '\t') {
          auto it = editor_state->buffers()->find(kPredictionsBufferName);
          if (it == editor_state->buffers()->end()) {
            editor_state->SetStatus(L"Error: predictions buffer not found.");
            return;
          }
          it->second->set_current_position_line(0);
          editor_state->set_current_buffer(it);
          editor_state->ScheduleRedraw();
        } else {
          LOG(INFO) << "Triggering predictions from: " << input_;
          GetPredictionsBuffer(
              editor_state, predictor_, input_->ToString(),
              [editor_state, this](const wstring& prediction) {
                if (input_->ToString() == prediction) {
                  return;
                }
                LOG(INFO) << "Prediction advanced from " << input_ << " to "
                          << prediction;
                input_ = EditableString::New(prediction);
                UpdateStatus(editor_state);
                // We do this so that another \t will cause the predictors
                // buffer to be updated (since the input has changed).
                most_recent_char_ = '\n';
              });
        }
        break;

      case Terminal::CTRL_U:
        input_->Clear();
        break;

      case Terminal::UP_ARROW:
        {
          auto buffer = GetHistoryBuffer(editor_state, history_file_)->second;
          if (buffer == nullptr || buffer->contents()->empty()) { return; }
          LineColumn position = buffer->position();
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
          LineColumn position = buffer->position();
          if (position.line + 1 <= buffer->contents()->size()) {
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
    if (buffer == nullptr || buffer->line() == buffer->end()) {
      input_ = EditableString::New(L"");
      return;
    }
    input_ = EditableString::New(buffer->current_line()->ToString());
  }

  const wstring prompt_;
  // Name of the file in which the history for this prompt should be preserved.
  const wstring history_file_;
  LinePromptHandler handler_;
  Predictor predictor_;
  map<wstring, shared_ptr<OpenBuffer>>::iterator initial_buffer_;
  shared_ptr<EditableString> input_;
  int most_recent_char_;
};

// TODO: Receive parameters we copy by value; use std::move.
class LinePromptCommand : public Command {
 public:
  LinePromptCommand(const wstring& prompt,
                    const wstring& history_file,
                    const wstring& description,
                    LinePromptHandler handler,
                    Predictor predictor)
      : prompt_(prompt),
        history_file_(history_file),
        description_(description),
        handler_(handler),
        predictor_(predictor) {}

  const wstring Description() {
    return description_;
  }

  void ProcessInput(int, EditorState* editor_state) {
    Prompt(editor_state, prompt_, history_file_, L"", handler_, predictor_);
  }

 private:
  const wstring prompt_;
  const wstring history_file_;
  const wstring description_;
  LinePromptHandler handler_;
  Predictor predictor_;
};

}  // namespace

using std::unique_ptr;
using std::shared_ptr;

void Prompt(EditorState* editor_state,
            const wstring& prompt,
            const wstring& history_file,
            const wstring& initial_value,
            LinePromptHandler handler,
            Predictor predictor) {
  std::unique_ptr<LinePromptMode> line_prompt_mode(new LinePromptMode(
      prompt, history_file, initial_value, handler, predictor,
      editor_state->current_buffer()));
  auto history = GetHistoryBuffer(editor_state, history_file);
  history->second->set_current_position_line(
      history->second->contents()->size());
  line_prompt_mode->UpdateStatus(editor_state);
  editor_state->set_mode(std::move(line_prompt_mode));
  editor_state->set_status_prompt(true);
}

unique_ptr<Command> NewLinePromptCommand(
    const wstring& prompt,
    const wstring& history_file,
    const wstring& description,
    LinePromptHandler handler,
    Predictor predictor) {
  return std::move(unique_ptr<Command>(new LinePromptCommand(
      prompt, history_file, description, handler, predictor)));
}

}  // namespace afc
}  // namespace editor
