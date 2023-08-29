#include "src/set_variable_command.h"

#include <map>
#include <memory>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"

namespace afc::editor {
namespace gc = language::gc;

using futures::DeleteNotification;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::NonNull;
using language::lazy_string::LazyString;

namespace {

std::wstring TrimWhitespace(const std::wstring& in) {
  size_t begin = in.find_first_not_of(' ', 0);
  if (begin == std::wstring::npos) {
    return L"";
  }
  size_t end = in.find_last_not_of(' ', in.size());
  if (end == std::wstring::npos) {
    return L"";
  }
  if (begin == 0 && end == in.size()) {
    return in;
  }
  return in.substr(begin, end - begin + 1);
}

Predictor VariablesPredictor() {
  std::vector<std::wstring> variables;
  buffer_variables::BoolStruct()->RegisterVariableNames(&variables);
  buffer_variables::StringStruct()->RegisterVariableNames(&variables);
  buffer_variables::IntStruct()->RegisterVariableNames(&variables);
  buffer_variables::DoubleStruct()->RegisterVariableNames(&variables);
  return PrecomputedPredictor(variables, '_');
}
}  // namespace

futures::Value<EmptyValue> SetVariableCommandHandler(
    EditorState& editor_state,
    NonNull<std::shared_ptr<LazyString>> input_name) {
  // TODO(easy, 2022-06-05): Get rid of ToString.
  std::wstring name = TrimWhitespace(input_name->ToString());
  if (name.empty()) {
    return futures::Past(EmptyValue());
  }

  std::vector<gc::Root<OpenBuffer>> active_buffers =
      editor_state.active_buffers();
  CHECK_GE(active_buffers.size(), 1ul);
  Status& default_error_status = active_buffers.size() == 1
                                     ? active_buffers[0].ptr()->status()
                                     : editor_state.status();
  const HistoryFile history_file(L"values");
  if (auto var = buffer_variables::StringStruct()->find_variable(name);
      var != nullptr) {
    Prompt(
        {.editor_state = editor_state,
         .prompt = name + L" := ",
         .history_file = history_file,
         .initial_value = active_buffers[0].ptr()->Read(var),
         .handler =
             [&editor_state, var](NonNull<std::shared_ptr<LazyString>> input) {
               editor_state.ResetRepetitions();
               return editor_state.ForEachActiveBuffer(
                   [var, input](OpenBuffer& buffer) {
                     // TODO(easy, 2022-06-05): Get rid of calls to ToString.
                     buffer.Set(var, input->ToString());
                     // TODO(easy, 2022-06-05): Get rid of calls to ToString.
                     buffer.status().SetInformationText(var->name() + L" := " +
                                                        input->ToString());
                     return futures::Past(EmptyValue());
                   });
             },
         .cancel_handler = []() { /* Nothing. */ },
         .predictor = var->predictor(),
         .status = PromptOptions::Status::kBuffer});
    return futures::Past(EmptyValue());
  }

  if (auto var = editor_variables::BoolStruct()->find_variable(name);
      var != nullptr) {
    editor_state.toggle_bool_variable(var);
    editor_state.ResetRepetitions();
    editor_state.status().SetInformationText(
        (editor_state.Read(var) ? L"🗸 " : L"⛶ ") + name);
    return futures::Past(EmptyValue());
  }
  if (auto var = editor_variables::DoubleStruct()->find_variable(name);
      var != nullptr) {
    Prompt({.editor_state = editor_state,
            .prompt = name + L" := ",
            .history_file = history_file,
            .initial_value = std::to_wstring(editor_state.Read(var)),
            .handler =
                [&editor_state, var, &default_error_status](
                    NonNull<std::shared_ptr<LazyString>> input) {
                  // TODO(easy, 2022-06-05): Get rid of ToString.
                  std::wstringstream ss(input->ToString());
                  double value;
                  ss >> value;
                  if (ss.eof() && !ss.fail()) {
                    editor_state.Set(var, value);
                  } else {
                    // TODO(easy, 2022-06-05): Get rid of ToString.
                    default_error_status.InsertError(
                        Error(L"Invalid value for double value “" +
                              var->name() + L"”: " + input->ToString()));
                  }
                  return futures::Past(EmptyValue());
                },
            .cancel_handler = []() { /* Nothing. */ },
            .status = PromptOptions::Status::kEditor});
    return futures::Past(EmptyValue());
  }
  if (auto var = buffer_variables::BoolStruct()->find_variable(name);
      var != nullptr) {
    return editor_state
        .ForEachActiveBuffer([var, name](OpenBuffer& buffer) {
          buffer.toggle_bool_variable(var);
          buffer.status().SetInformationText(
              (buffer.Read(var) ? L"🗸 " : L"⛶ ") + name);
          return futures::Past(EmptyValue());
        })
        .Transform([&editor_state](EmptyValue) {
          editor_state.ResetRepetitions();
          return EmptyValue();
        });
  }
  if (auto var = buffer_variables::IntStruct()->find_variable(name);
      var != nullptr) {
    Prompt(PromptOptions{
        .editor_state = editor_state,
        .prompt = name + L" := ",
        .history_file = history_file,
        .initial_value = std::to_wstring(active_buffers[0].ptr()->Read(var)),
        .handler =
            [&editor_state, var, &default_error_status](
                NonNull<std::shared_ptr<LazyString>> input) {
              int value;
              try {
                // TODO(easy, 2022-06-05): Get rid of ToString.
                value = stoi(input->ToString());
              } catch (const std::invalid_argument& ia) {
                default_error_status.InsertError(
                    Error(L"Invalid value for integer value “" + var->name() +
                          L"”: " + FromByteString(ia.what())));
                return futures::Past(EmptyValue());
              }
              editor_state.ForEachActiveBuffer(
                  [var, value](OpenBuffer& buffer) {
                    buffer.Set(var, value);
                    return futures::Past(EmptyValue());
                  });
              return futures::Past(EmptyValue());
            },
        .cancel_handler = []() { /* Nothing. */ },
        .predictor = var->predictor(),
        .status = PromptOptions::Status::kBuffer});
    return futures::Past(EmptyValue());
  }
  if (auto var = buffer_variables::DoubleStruct()->find_variable(name);
      var != nullptr) {
    Prompt(PromptOptions{
        .editor_state = editor_state,
        .prompt = name + L" := ",
        .history_file = history_file,
        .initial_value = std::to_wstring(active_buffers[0].ptr()->Read(var)),
        .handler =
            [&editor_state, var, &default_error_status](
                NonNull<std::shared_ptr<LazyString>> input) {
              // TODO(easy, 2022-06-05): Get rid of ToString.
              std::wstringstream ss(input->ToString());
              double value;
              ss >> value;
              if (ss.eof() && !ss.fail()) {
                editor_state.ForEachActiveBuffer(
                    [var, value](OpenBuffer& buffer) {
                      buffer.Set(var, value);
                      return futures::Past(EmptyValue());
                    });
              } else {
                // TODO(easy, 2022-06-05): Get rid of ToString.
                default_error_status.InsertError(
                    Error(L"Invalid value for double value “" + var->name() +
                          L"”: " + input->ToString()));
              }
              return futures::Past(EmptyValue());
            },
        .cancel_handler = []() { /* Nothing. */ },
        .status = PromptOptions::Status::kBuffer});
    return futures::Past(EmptyValue());
  }

  default_error_status.InsertError(Error(L"Unknown variable: " + name));
  return futures::Past(EmptyValue());
}

NonNull<std::unique_ptr<Command>> NewSetVariableCommand(
    EditorState& editor_state) {
  static Predictor variables_predictor = VariablesPredictor();
  return NewLinePromptCommand(
      editor_state, L"assigns to a variable", [&editor_state] {
        return PromptOptions{
            .editor_state = editor_state,
            .prompt = L"🔧 ",
            .history_file = HistoryFile(L"variables"),
            .colorize_options_provider =
                [&editor_state, variables_predictor = variables_predictor](
                    const NonNull<std::shared_ptr<LazyString>>& line,
                    NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
                    DeleteNotification::Value abort_value)
                -> futures::Value<ColorizePromptOptions> {
              return Predict(
                         PredictOptions{
                             .editor_state = editor_state,
                             .predictor = variables_predictor,
                             .text = line->ToString(),
                             .source_buffers = editor_state.active_buffers(),
                             .progress_channel =
                                 std::move(progress_channel.get_unique()),
                             .abort_value = std::move(abort_value)})
                  .Transform([line](std::optional<PredictResults> results) {
                    return ColorizePromptOptions{
                        .context = results.has_value()
                                       ? results->predictions_buffer
                                       : std::optional<gc::Root<OpenBuffer>>()};
                  });
            },
            .handler = std::bind_front(SetVariableCommandHandler,
                                       std::ref(editor_state)),
            .cancel_handler = []() { /* Nothing. */ },
            .predictor = variables_predictor,
            .status = PromptOptions::Status::kBuffer};
      });
}

}  // namespace afc::editor
