#include "line_prompt_mode.h"

#include <memory>
#include <limits>
#include <string>

#include "buffer.h"
#include "char_buffer.h"
#include "command.h"
#include "command_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "editor_mode.h"
#include "file_link_mode.h"
#include "insert_mode.h"
#include "predictor.h"
#include "transformation_delete.h"
#include "terminal.h"
#include "wstring.h"

namespace afc {
namespace editor {
namespace {

using std::make_pair;

void UpdateStatus(EditorState* editor_state, OpenBuffer* buffer,
                  const wstring& prompt) {
  DCHECK(buffer != nullptr);
  auto line = buffer->current_line();
  wstring input = line == nullptr ? L"" : line->contents()->ToString();
  editor_state->SetStatus(prompt + input);
  editor_state->set_status_prompt(true);
  editor_state->set_status_prompt_column(
      prompt.size()
      + min(input.size(), buffer->current_position_col()));
}

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
  it->second->set_bool_variable(
      OpenBuffer::variable_show_in_buffers_list(), false);
  it->second->set_bool_variable(
      OpenBuffer::variable_atomic_lines(), true);
  if (!editor_state->has_current_buffer()) {
    // Seems lame, but what can we do?
    editor_state->set_current_buffer(it);
    editor_state->ScheduleRedraw();
  }
  return it;
}

shared_ptr<OpenBuffer> GetPromptBuffer(EditorState* editor_state) {
  auto& element =
      *editor_state->buffers()->insert(make_pair(L"- prompt", nullptr)).first;
  if (element.second == nullptr) {
    element.second = std::make_shared<OpenBuffer>(editor_state, element.first);
    element.second->set_bool_variable(
        OpenBuffer::variable_allow_dirty_delete(), true);
    element.second->set_bool_variable(
        OpenBuffer::variable_show_in_buffers_list(), false);
    element.second->set_bool_variable(
        OpenBuffer::variable_delete_into_paste_buffer(), false);
  } else {
    element.second->ClearContents(editor_state);
  }
  return element.second;
}

class HistoryScrollBehavior : public ScrollBehavior {
 public:
  HistoryScrollBehavior(wstring history_file, wstring prompt)
      : history_file_(history_file),
        prompt_(prompt) {}

  void Up(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollHistory(editor_state, buffer, -1);
  }

  void Down(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollHistory(editor_state, buffer, +1);
  }

  void Left(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollBehavior::Default()->Left(editor_state, buffer);
    UpdateStatus(editor_state, buffer, prompt_);
  }

  void Right(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollBehavior::Default()->Right(editor_state, buffer);
    UpdateStatus(editor_state, buffer, prompt_);
  }

  void Begin(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollBehavior::Default()->Begin(editor_state, buffer);
    UpdateStatus(editor_state, buffer, prompt_);
  }

  void End(EditorState* editor_state, OpenBuffer* buffer) const override {
    ScrollBehavior::Default()->End(editor_state, buffer);
    UpdateStatus(editor_state, buffer, prompt_);
  }

 private:
  void ScrollHistory(EditorState* editor_state, OpenBuffer* buffer, int delta) const {
    auto insert =
        std::make_shared<OpenBuffer>(editor_state, L"- text inserted");

    auto history = GetHistoryBuffer(editor_state, history_file_);
    if (history->second != nullptr && history->second->contents()->size() > 1) {
      auto previous_buffer = editor_state->current_buffer()->second;
      editor_state->set_current_buffer(history);
      history->second->set_mode(previous_buffer->ResetMode());

      LineColumn position = history->second->position();
      position.line += delta;
      if (position.line <= history->second->contents()->size() &&
          position.line > 0) {
        history->second->set_position(position);
      }
      if (history->second->current_line() != nullptr) {
        insert->AppendToLastLine(
            editor_state, history->second->current_line()->contents());
      }
    }

    DeleteOptions delete_options;
    delete_options.copy_to_paste_buffer = false;
    buffer->ApplyToCursors(NewDeleteLinesTransformation(delete_options));
    buffer->ApplyToCursors(NewInsertBufferTransformation(insert, 1, END));

    UpdateStatus(editor_state, buffer, prompt_);
  }

  const wstring history_file_;
  const wstring prompt_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(wstring description,
                    std::function<PromptOptions(EditorState*)> options)
      : description_(std::move(description)),
        options_(std::move(options)) {}

  const wstring Description() {
    return description_;
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    Prompt(editor_state, options_(editor_state));
  }

 private:
  const wstring description_;
  std::function<PromptOptions(EditorState*)> options_;
};


}  // namespace

using std::unique_ptr;
using std::shared_ptr;

void Prompt(EditorState* editor_state, PromptOptions options) {
  CHECK(options.handler);
  auto history = GetHistoryBuffer(editor_state, options.history_file);
  history->second->set_current_position_line(
      history->second->contents()->size());

  auto buffer = GetPromptBuffer(editor_state);
  Modifiers original_modifiers = editor_state->modifiers();
  editor_state->set_modifiers(Modifiers());

  {
    auto insert =
        std::make_shared<OpenBuffer>(editor_state, L"- text inserted");
    insert->AppendToLastLine(editor_state,
                             NewCopyString(options.initial_value));
    buffer->ApplyToCursors(NewInsertBufferTransformation(insert, 1, END));
  }

  InsertModeOptions insert_mode_options;
  insert_mode_options.editor_state = editor_state;
  insert_mode_options.buffer = buffer;

  auto original_buffer = editor_state->current_buffer();
  insert_mode_options.modify_listener =
      [editor_state, original_buffer, buffer, options]() {
        editor_state->set_current_buffer(original_buffer);
        UpdateStatus(editor_state, buffer.get(), options.prompt);
      };

  insert_mode_options.scroll_behavior = std::make_shared<HistoryScrollBehavior>(
      options.history_file, options.prompt);

  insert_mode_options.escape_handler =
      [editor_state, options, original_buffer, original_modifiers]() {
        LOG(INFO) << "Running escape_handler from Prompt.";
        editor_state->set_current_buffer(original_buffer);
        editor_state->set_modifiers(original_modifiers);
        editor_state->set_status_prompt(false);
        editor_state->ScheduleRedraw();

        // We make a copy in case cancel_handler or handler delete us.
        auto buffer = original_buffer->second;
        if (options.cancel_handler) {
          VLOG(5) << "Running cancel handler.";
          options.cancel_handler(editor_state);
        } else {
          VLOG(5) << "Running handler on empty input.";
          options.handler(L"", editor_state);
        }
        buffer->ResetMode();
        editor_state->set_keyboard_redirect(nullptr);
      };

  insert_mode_options.new_line_handler =
      [editor_state, options, buffer, original_buffer, original_modifiers]() {
        editor_state->set_current_buffer(original_buffer);
        auto input = buffer->current_line()->contents();
        if (input->size() != 0) {
          auto history =
              GetHistoryBuffer(editor_state, options.history_file)->second;
          CHECK(history != nullptr);
          if (history->contents()->size() == 0
              || (history->contents()->at(history->contents()->size() - 1)
                      ->ToString()
                  != input->ToString())) {
            history->AppendLine(editor_state, input);
          }
        }
        auto ensure_survival_of_current_closure = editor_state->keyboard_redirect();
        editor_state->set_keyboard_redirect(nullptr);
        editor_state->set_status_prompt(false);
        editor_state->ResetStatus();
        editor_state->set_modifiers(original_modifiers);
        options.handler(input->ToString(), editor_state);
        (void) ensure_survival_of_current_closure;
      };

  insert_mode_options.start_completion = [editor_state, options, buffer]() {
    auto input = buffer->current_line()->contents()->ToString();
    LOG(INFO) << "Triggering predictions from: " << input;
    Predict(
        editor_state, options.predictor, input,
        [editor_state, options, buffer, input](const wstring& prediction) {
          if (input.size() < prediction.size()) {
            LOG(INFO) << "Prediction advanced from " << input << " to "
                      << prediction;

            DeleteOptions delete_options;
            delete_options.copy_to_paste_buffer = false;
            buffer->ApplyToCursors(
                NewDeleteLinesTransformation(delete_options));

            auto insert =
                std::make_shared<OpenBuffer>(editor_state, L"- text inserted");
            insert->AppendToLastLine(editor_state, NewCopyString(prediction));
            buffer->ApplyToCursors(
                NewInsertBufferTransformation(insert, 1, END));

            UpdateStatus(editor_state, buffer.get(), options.prompt);
            editor_state->ScheduleRedraw();
          } else {
            LOG(INFO) << "Prediction didn't advance.";
            auto it = editor_state->buffers()->find(PredictionsBufferName());
            if (it == editor_state->buffers()->end()) {
              editor_state->SetWarningStatus(
                  L"Error: predictions buffer not found.");
            } else {
              it->second->set_current_position_line(0);
              editor_state->set_current_buffer(it);
              editor_state->ScheduleRedraw();
            }
          }
        });
    return true;
  };

  EnterInsertMode(insert_mode_options);
  insert_mode_options.modify_listener();
}

unique_ptr<Command> NewLinePromptCommand(
    wstring description, std::function<PromptOptions(EditorState*)> options) {
  return std::move(unique_ptr<Command>(new LinePromptCommand(
      std::move(description), std::move(options))));
}

}  // namespace afc
}  // namespace editor
