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

futures::Value<bool> SetVariableHandler(const wstring& input_name,
                                        EditorState* editor_state) {
  wstring name = TrimWhitespace(input_name);
  if (name.empty()) {
    return futures::Past(true);
  }

  PromptOptions options;
  options.editor_state = editor_state;
  options.prompt = name + L" := ";
  options.history_file = L"values",
  options.status = PromptOptions::Status::kBuffer;

  auto active_buffers = editor_state->active_buffers();
  CHECK_GE(active_buffers.size(), 1);
  auto default_error_status = active_buffers.size() == 1
                                  ? active_buffers[0]->status()
                                  : editor_state->status();
  if (auto var = buffer_variables::StringStruct()->find_variable(name);
      var != nullptr) {
    options.initial_value = active_buffers[0]->Read(var);
    options.handler = [var](const wstring& input, EditorState* editor) {
      editor->ForEachActiveBuffer(
          [var, input](const std::shared_ptr<OpenBuffer>& buffer) {
            buffer->Set(var, input);
            // TODO(easy): Update status.
            return futures::Past(true);
          });
      return futures::Past(true);
    };
    options.cancel_handler = [](EditorState*) { /* Nothing. */ };
    options.predictor = var->predictor();
    Prompt(std::move(options));
    return futures::Past(true);
  }

  if (auto var = editor_variables::BoolStruct()->find_variable(name);
      var != nullptr) {
    editor_state->toggle_bool_variable(var);
    editor_state->status()->SetInformationText(
        (editor_state->Read(var) ? L"🗸 " : L"⛶ ") + name);
    return futures::Past(true);
  }

  if (auto var = buffer_variables::BoolStruct()->find_variable(name);
      var != nullptr) {
    editor_state->ForEachActiveBuffer(
        [var, name](const std::shared_ptr<OpenBuffer>& buffer) {
          buffer->toggle_bool_variable(var);
          buffer->status()->SetInformationText(
              (buffer->Read(var) ? L"🗸 " : L"⛶ ") + name);
          return futures::Past(true);
        });
    return futures::Past(true);
  }
  if (auto var = buffer_variables::IntStruct()->find_variable(name);
      var != nullptr) {
    options.initial_value = std::to_wstring(active_buffers[0]->Read(var));
    options.handler = [var, default_error_status](const wstring& input,
                                                  EditorState* editor) {
      int value;
      try {
        value = stoi(input);
      } catch (const std::invalid_argument& ia) {
        default_error_status->SetWarningText(
            L"Invalid value for integer value “" + var->name() + L"”: " +
            FromByteString(ia.what()));
        return futures::Past(true);
      }
      editor->ForEachActiveBuffer(
          [var, value](const std::shared_ptr<OpenBuffer>& buffer) {
            buffer->Set(var, value);
            return futures::Past(true);
          });
      return futures::Past(true);
    };
    options.cancel_handler = [](EditorState*) { /* Nothing. */ };
    Prompt(std::move(options));
    return futures::Past(true);
  }
  if (auto var = buffer_variables::DoubleStruct()->find_variable(name);
      var != nullptr) {
    options.initial_value = std::to_wstring(active_buffers[0]->Read(var));
    options.handler = [var, default_error_status](const wstring& input,
                                                  EditorState* editor) {
      std::wstringstream ss(input);
      double value;
      ss >> value;
      if (ss.eof() && !ss.fail()) {
        editor->ForEachActiveBuffer(
            [var, value](const std::shared_ptr<OpenBuffer>& buffer) {
              buffer->Set(var, value);
              return futures::Past(true);
            });
      } else {
        default_error_status->SetWarningText(
            L"Invalid value for double value “" + var->name() + L"”: " + input);
      }
      return futures::Past(true);
    };
    options.cancel_handler = [](EditorState*) { /* Nothing. */ };
    Prompt(std::move(options));
    return futures::Past(true);
  }

  default_error_status->SetWarningText(L"Unknown variable: " + name);
  return futures::Past(true);
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

unique_ptr<Command> NewSetVariableCommand(EditorState* editor_state) {
  static Predictor variables_predictor = VariablesPredictor();
  PromptOptions options;
  options.editor_state = editor_state;
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
