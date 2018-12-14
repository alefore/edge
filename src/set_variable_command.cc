#include <map>
#include <memory>
#include <string>

#include "buffer.h"
#include "buffer_variables.h"
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
  wstring name = TrimWhitespace(input_name);
  if (name.empty()) {
    return;
  }

  if (!editor_state->has_current_buffer()) {
    return;
  }
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
    auto var = buffer_variables::StringStruct()->find_variable(name);
    if (var != nullptr) {
      PromptOptions options;
      options.prompt = name + L" := ";
      options.history_file = L"values";
      options.initial_value = buffer->read_string_variable(var);
      options.handler = [var, buffer](const wstring& input, EditorState*) {
        if (buffer == nullptr) {
          return;
        }
        buffer->set_string_variable(var, input);
        // ResetMode causes the prompt to be deleted, and the captures of
        // this lambda go away with it.
        buffer->ResetMode();
      };
      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      options.predictor = var->predictor();
      Prompt(editor_state, std::move(options));
      return;
    }
  }
  {
    auto var = buffer_variables::BoolStruct()->find_variable(name);
    if (var != nullptr) {
      buffer->toggle_bool_variable(var);
      editor_state->SetStatus(
          name + L" := " + (buffer->read_bool_variable(var) ? L"ON" : L"OFF"));
      return;
    }
  }
  {
    auto var = buffer_variables::IntStruct()->find_variable(name);
    if (var != nullptr) {
      PromptOptions options;
      options.prompt = name + L" := ", options.history_file = L"values",
      options.initial_value = std::to_wstring(buffer->Read(var));
      options.handler = [var, buffer](const wstring& input,
                                      EditorState* editor_state) {
        try {
          buffer->set_int_variable(var, stoi(input));
        } catch (const std::invalid_argument& ia) {
          editor_state->SetStatus(L"Invalid value for integer value “" +
                                  var->name() + L"”: " +
                                  FromByteString(ia.what()));
        }
        // ResetMode causes the prompt to be deleted, and the captures of
        // this lambda go away with it.
        buffer->ResetMode();
      };
      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      Prompt(editor_state, std::move(options));
      return;
    }
  }
  {
    auto var = buffer_variables::DoubleStruct()->find_variable(name);
    if (var != nullptr) {
      PromptOptions options;
      options.prompt = name + L" := ", options.history_file = L"values",
      options.initial_value = std::to_wstring(buffer->Read(var));
      options.handler = [var, buffer](const wstring& input,
                                      EditorState* editor_state) {
        std::wstringstream ss(input);
        double value;
        ss >> value;
        if (ss.eof() && !ss.fail()) {
          buffer->set_double_variable(var, value);
        } else {
          editor_state->SetStatus(L"Invalid value for double value “" +
                                  var->name() + L"”: " + input);
        }
        // ResetMode causes the prompt to be deleted, and the captures of
        // this lambda go away with it.
        buffer->ResetMode();
      };
      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      Prompt(editor_state, std::move(options));
      return;
    }
  }
  editor_state->SetWarningStatus(L"Unknown variable: " + name);
}

Predictor VariablesPredictor() {
  vector<wstring> variables;
  buffer_variables::BoolStruct()->RegisterVariableNames(&variables);
  buffer_variables::StringStruct()->RegisterVariableNames(&variables);
  buffer_variables::IntStruct()->RegisterVariableNames(&variables);
  buffer_variables::DoubleStruct()->RegisterVariableNames(&variables);
  return PrecomputedPredictor(variables, '_');
}
}  // namespace

unique_ptr<Command> NewSetVariableCommand() {
  static Predictor variables_predictor = VariablesPredictor();
  PromptOptions options;
  options.prompt = L"var ";
  options.history_file = L"variables";
  options.handler = SetVariableHandler;
  options.cancel_handler = [](EditorState*) { /* Nothing. */ };
  options.predictor = variables_predictor;
  return NewLinePromptCommand(L"assigns to a variable",
                              [options](EditorState*) { return options; });
}

}  // namespace editor
}  // namespace afc
