#include <map>
#include <memory>
#include <string>

#include "buffer.h"
#include "command_mode.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "wstring.h"

namespace afc {
namespace editor {

using std::wstring;

namespace {

wstring TrimWhitespace(const wstring& in) {
  size_t begin = in.find_first_not_of(' ', 0);
  if (begin == string::npos) {
    return L"";
  }
  size_t end = in.find_last_not_of(' ', in.size());
  if (end == string::npos) {
    return L"";
  }
  if (begin == 0 && end == in.size()) {
    return in;
  }
  return in.substr(begin, end - begin + 1);
}

void SetVariableCancelHandler(EditorState* editor_state) {
  editor_state->ResetMode();
  editor_state->ScheduleRedraw();
}

void SetVariableHandler(const wstring& input_name, EditorState* editor_state) {
  SetVariableCancelHandler(editor_state);
  wstring name = TrimWhitespace(input_name);
  if (name.empty()) { return; }

  if (!editor_state->has_current_buffer()) { return; }
  auto buffer = editor_state->current_buffer()->second;
  CHECK(buffer != nullptr);
  if (editor_state->modifiers().structure == LINE) {
    auto target_buffer = buffer->GetBufferFromCurrentLine();
    if (target_buffer != nullptr) {
      buffer = target_buffer;
    }
    editor_state->ResetModifiers();
  }

  {
    auto var = OpenBuffer::StringStruct()->find_variable(name);
    if (var != nullptr) {
      PromptOptions options;
      options.prompt = name + L" := ";
      options.history_file = L"values";
      options.initial_value = buffer->read_string_variable(var);
      options.handler =
          [var, buffer](const wstring& input, EditorState* editor_state) {
            if (buffer != nullptr) {
              buffer->set_string_variable(var, input);
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          };
      options.predictor = var->predictor();
      Prompt(editor_state, std::move(options));
      return;
    }
  }
  {
    auto var = OpenBuffer::BoolStruct()->find_variable(name);
    if (var != nullptr) {
      buffer->toggle_bool_variable(var);
      editor_state->SetStatus(
          name + L" := " + (buffer->read_bool_variable(var) ? L"ON" : L"OFF"));
      return;
    }
  }
  {
    auto var = OpenBuffer::IntStruct()->find_variable(name);
    if (var != nullptr) {
      PromptOptions options;
      options.prompt = name + L" := ",
      options.history_file = L"values",
      options.initial_value = std::to_wstring(buffer->read_int_variable(var));
      options.handler =
          [var, buffer](const wstring& input, EditorState* editor_state) {
            try {
              buffer->set_int_variable(var, stoi(input));
            } catch (const std::invalid_argument& ia) {
              editor_state->SetStatus(
                  L"Invalid value for integer value “" + var->name() + L"”: " +
                  FromByteString(ia.what()));
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          };
      Prompt(editor_state, std::move(options));
      return;
    }
  }
  editor_state->SetStatus(L"Unknown variable: " + name);
}

}  // namespace

unique_ptr<Command> NewSetVariableCommand() {
  static vector<wstring> variables;
  if (variables.empty()) {
    OpenBuffer::BoolStruct()->RegisterVariableNames(&variables);
    OpenBuffer::StringStruct()->RegisterVariableNames(&variables);
    OpenBuffer::IntStruct()->RegisterVariableNames(&variables);
  }

  PromptOptions options;
  options.prompt = L"var ";
  options.history_file = L"variables";
  options.handler = SetVariableHandler;
  options.cancel_handler = SetVariableCancelHandler;
  options.predictor = PrecomputedPredictor(variables, '_');
  return NewLinePromptCommand(L"assigns to a variable", std::move(options));
}

}  // namespace afc
}  // namespace editor
