#include "src/set_variable_command.h"

#include <map>
#include <memory>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::futures::DeleteNotification;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NonNull;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Trim;
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc::editor {
namespace {
Predictor VariablesPredictor() {
  // We need to materialize the nested vector because, even though all ranges
  // contain the same types (LazyString), they actually have different types
  // (because they are keys of maps with different value types).
  return PrecomputedPredictor(
      container::MaterializeVector(
          std::vector{container::MaterializeVector(
                          buffer_variables::BoolStruct()->VariableNames()),
                      container::MaterializeVector(
                          buffer_variables::StringStruct()->VariableNames()),
                      container::MaterializeVector(
                          buffer_variables::IntStruct()->VariableNames()),
                      container::MaterializeVector(
                          buffer_variables::DoubleStruct()->VariableNames())} |
          std::views::join),
      '_');
}
}  // namespace

futures::Value<EmptyValue> SetVariableCommandHandler(EditorState& editor_state,
                                                     SingleLine input_name) {
  // TODO(trivial, 2024-09-13): Get rid of read():
  LazyString name = Trim(input_name.read(), {L' '});
  LOG(INFO) << "SetVariableCommandHandler: " << input_name << " -> " << name;
  if (name.empty()) return futures::Past(EmptyValue());

  std::vector<gc::Root<OpenBuffer>> active_buffers =
      editor_state.active_buffers();
  if (active_buffers.size() != 1) return futures::Past(EmptyValue());
  Status& default_error_status = active_buffers.size() == 1
                                     ? active_buffers[0].ptr()->status()
                                     : editor_state.status();
  static const HistoryFile history_file{LazyString{L"values"}};
  if (auto var = buffer_variables::StringStruct()->find_variable(name);
      var != nullptr) {
    Prompt(
        {.editor_state = editor_state,
         .prompt = name + LazyString{L" := "},
         .history_file = history_file,
         .initial_value = Line{active_buffers[0].ptr()->ReadLazyString(var)},
         .handler =
             [&editor_state, var](SingleLine input) {
               editor_state.ResetRepetitions();
               return editor_state.ForEachActiveBuffer(
                   [var, input](OpenBuffer& buffer) {
                     buffer.Set(var, input.read());
                     buffer.status().SetInformationText(
                         // TODO(trivial, 2024-09-13): Get rid of read():
                         LineBuilder((SingleLine{var->name()} +
                                      SingleLine{LazyString{L" := "}} + input)
                                         .read())
                             .Build());
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
        LineBuilder(LazyString{editor_state.Read(var) ? L"🗸 " : L"⛶ "} + name)
            .Build());
    return futures::Past(EmptyValue());
  }
  if (auto var = editor_variables::DoubleStruct()->find_variable(name);
      var != nullptr) {
    Prompt({.editor_state = editor_state,
            .prompt = name + LazyString{L" := "},
            .history_file = history_file,
            .initial_value =
                Line{LazyString{std::to_wstring(editor_state.Read(var))}},
            .handler =
                [&editor_state, var, &default_error_status](SingleLine input) {
                  // TODO(easy, 2022-06-05): Get rid of ToString.
                  std::wstringstream ss(input.read().ToString());
                  double value;
                  ss >> value;
                  if (ss.eof() && !ss.fail()) {
                    editor_state.Set(var, value);
                  } else {
                    default_error_status.InsertError(
                        Error{LazyString{L"Invalid value for double value “"} +
                              var->name() + LazyString{L"”: "} + input.read()});
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
              LineBuilder(LazyString{(buffer.Read(var) ? L"🗸 " : L"⛶ ")} +
                          name)
                  .Build());
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
        .prompt = name + LazyString{L" := "},
        .history_file = history_file,
        .initial_value = Line{LazyString{
            std::to_wstring(active_buffers[0].ptr()->Read(var))}},
        .handler =
            [&editor_state, var, &default_error_status](SingleLine input) {
              int value;
              try {
                // TODO(easy, 2022-06-05): Get rid of ToString.
                value = stoi(input.read().ToString());
              } catch (const std::invalid_argument& ia) {
                default_error_status.InsertError(
                    Error{LazyString{L"Invalid value for integer value “"} +
                          var->name() + LazyString{L"”: "} +
                          LazyString{FromByteString(ia.what())}});
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
        .prompt = name + LazyString{L" := "},
        .history_file = history_file,
        .initial_value = Line{LazyString{
            std::to_wstring(active_buffers[0].ptr()->Read(var))}},
        .handler =
            [&editor_state, var, &default_error_status](SingleLine input) {
              // TODO(easy, 2022-06-05): Get rid of ToString.
              std::wstringstream ss(input.read().ToString());
              double value;
              ss >> value;
              if (ss.eof() && !ss.fail()) {
                editor_state.ForEachActiveBuffer(
                    [var, value](OpenBuffer& buffer) {
                      buffer.Set(var, value);
                      return futures::Past(EmptyValue());
                    });
              } else {
                default_error_status.InsertError(
                    Error{LazyString{L"Invalid value for double value “"} +
                          var->name() + LazyString{L"”: "} + input.read()});
              }
              return futures::Past(EmptyValue());
            },
        .cancel_handler = []() { /* Nothing. */ },
        .status = PromptOptions::Status::kBuffer});
    return futures::Past(EmptyValue());
  }

  default_error_status.InsertError(
      Error{LazyString{L"Unknown variable: "} + name});
  return futures::Past(EmptyValue());
}

gc::Root<Command> NewSetVariableCommand(EditorState& editor_state) {
  static Predictor variables_predictor = VariablesPredictor();
  return NewLinePromptCommand(
      editor_state, L"assigns to a variable", [&editor_state] {
        return PromptOptions{
            .editor_state = editor_state,
            .prompt = LazyString{L"🔧 "},
            .history_file = HistoryFile{LazyString{L"variables"}},
            .colorize_options_provider =
                [&editor_state, variables_predictor = variables_predictor](
                    const SingleLine& line,
                    NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
                    DeleteNotification::Value abort_value)
                -> futures::Value<ColorizePromptOptions> {
              return Predict(
                         variables_predictor,
                         PredictorInput{
                             .editor = editor_state,
                             // TODO(trivial, 2024-09-13): Get rid of read().
                             .input = line.read(),
                             .input_column = ColumnNumber() + line.size(),
                             .source_buffers = editor_state.active_buffers(),
                             .progress_channel = std::move(progress_channel),
                             .abort_value = std::move(abort_value)})
                  .Transform([](std::optional<PredictResults>
                                    results_optional) {
                    return VisitOptional(
                        [](PredictResults results) {
                          return ColorizePromptOptions{
                              .context = ColorizePromptOptions::ContextBuffer{
                                  std::move(results.predictions_buffer)}};
                        },
                        [] {
                          return ColorizePromptOptions{
                              .context = ColorizePromptOptions::ContextClear{}};
                        },
                        std::move(results_optional));
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
