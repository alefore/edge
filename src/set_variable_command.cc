#include "src/set_variable_command.h"

#include <map>
#include <memory>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"
#include "src/wstring.h"

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

  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  if (editor_state->modifiers().structure == StructureLine()) {
    auto target_buffer = buffer->GetBufferFromCurrentLine();
    if (target_buffer != nullptr) {
      buffer = target_buffer;
    }
    editor_state->ResetModifiers();
  }

  PromptOptions options;
  options.prompt = name + L" := ";
  options.history_file = L"values",
  options.status = PromptOptions::Status::kBuffer;

  {
    auto var = buffer_variables::StringStruct()->find_variable(name);
    if (var != nullptr) {
      options.initial_value = buffer->Read(var);
      options.handler = [var, buffer](const wstring& input, EditorState*) {
        if (buffer == nullptr) {
          return;
        }
        buffer->Set(var, input);
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
      buffer->status()->SetInformationText(
          (buffer->Read(var) ? L"🗸 " : L"⛶ ") + name);
      return;
    }
  }
  {
    auto var = buffer_variables::IntStruct()->find_variable(name);
    if (var != nullptr) {
      options.initial_value = std::to_wstring(buffer->Read(var));
      options.handler = [var, buffer](const wstring& input,
                                      EditorState* editor_state) {
        try {
          buffer->Set(var, stoi(input));
        } catch (const std::invalid_argument& ia) {
          buffer->status()->SetWarningText(
              L"Invalid value for integer value “" + var->name() + L"”: " +
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
      options.initial_value = std::to_wstring(buffer->Read(var));
      options.handler = [var, buffer](const wstring& input,
                                      EditorState* editor_state) {
        std::wstringstream ss(input);
        double value;
        ss >> value;
        if (ss.eof() && !ss.fail()) {
          buffer->Set(var, value);
        } else {
          buffer->status()->SetWarningText(L"Invalid value for double value “" +
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
  buffer->status()->SetWarningText(L"Unknown variable: " + name);
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
  options.prompt = L"🔧 ";
  options.history_file = L"variables";
  options.handler = SetVariableHandler;
  options.cancel_handler = [](EditorState*) { /* Nothing. */ };
  options.predictor = variables_predictor;
  options.status = PromptOptions::Status::kBuffer;
  return NewLinePromptCommand(L"assigns to a variable",
                              [options](EditorState*) { return options; });
}

}  // namespace editor
}  // namespace afc
