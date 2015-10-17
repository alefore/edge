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

void SetVariableHandler(const wstring& input_name, EditorState* editor_state) {
  editor_state->ResetMode();
  editor_state->ScheduleRedraw();
  wstring name = TrimWhitespace(input_name);
  if (name.empty()) { return; }
  {
    auto var = OpenBuffer::StringStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      Prompt(
          editor_state,
          name + L" := ",
          L"values",
          editor_state->current_buffer()->second->read_string_variable(var),
          [var](const wstring& input, EditorState* editor_state) {
            if (editor_state->has_current_buffer()) {
              editor_state->current_buffer()
                  ->second->set_string_variable(var, input);
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          },
          var->predictor());
      return;
    }
  }
  {
    auto var = OpenBuffer::BoolStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      auto buffer = editor_state->current_buffer()->second;
      buffer->toggle_bool_variable(var);
      editor_state->SetStatus(
          name + L" := " + (buffer->read_bool_variable(var) ? L"ON" : L"OFF"));
      return;
    }
  }
  {
    auto var = OpenBuffer::IntStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      auto buffer = editor_state->current_buffer()->second;
      Prompt(
          editor_state,
          name + L" := ",
          L"values",
          std::to_wstring(
              editor_state->current_buffer()->second->read_int_variable(var)),
          [var](const wstring& input, EditorState* editor_state) {
            if (!editor_state->has_current_buffer()) { return; }
            try {
              editor_state->current_buffer()
                  ->second->set_int_variable(var, stoi(input));
            } catch (const std::invalid_argument& ia) {
              editor_state->SetStatus(
                  L"Invalid value for integer value “" + var->name() + L"”: " +
                  FromByteString(ia.what()));
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          },
          &EmptyPredictor);
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

  return NewLinePromptCommand(
      L"var ", L"variables", L"assigns to a variable", SetVariableHandler,
      PrecomputedPredictor(variables, '_'));
}

}  // namespace afc
}  // namespace editor
