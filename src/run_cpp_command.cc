#include "src/run_cpp_command.h"

#include <memory>

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/line_prompt_mode.h"
#include "src/substring.h"
#include "src/tokenize.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

namespace afc::editor {
namespace {

futures::Value<EmptyValue> RunCppCommandLiteralHandler(
    const wstring& name, EditorState* editor_state) {
  // TODO(easy): Honor `multiple_buffers`.
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(EmptyValue());
  }
  buffer->ResetMode();
  buffer->EvaluateString(name);
  return futures::Past(EmptyValue());
}

struct ParsedCommand {
  std::vector<Token> tokens;
  vm::Value* function = nullptr;
  std::vector<std::unique_ptr<Expression>> inputs;
};

ValueOrError<ParsedCommand> Parse(
    std::shared_ptr<LazyString> command, Environment* environment,
    std::shared_ptr<LazyString> function_name_prefix,
    std::unordered_set<VMType> accepted_return_types) {
  ParsedCommand output;
  output.tokens = TokenizeBySpaces(*command);
  if (output.tokens.empty()) {
    // Deliberately empty so as to not trigger a status update.
    return Error(L"");
  }

  std::vector<Value*> functions;
  environment->CaseInsensitiveLookup(
      StringAppend(function_name_prefix, NewLazyString(output.tokens[0].value))
          ->ToString(),
      &functions);

  if (functions.empty()) {
    return Error(L"Unknown symbol: " + output.tokens[0].value);
  }

  // Filter functions that match our type expectations.
  std::vector<Value*> type_match_functions;
  Value* function_vector = nullptr;
  for (auto& candidate : functions) {
    if (!candidate->IsFunction()) {
      continue;
    }
    const auto& arguments = candidate->type.type_arguments;
    if (accepted_return_types.find(arguments[0]) ==
        accepted_return_types.end()) {
      continue;
    }

    bool all_arguments_are_strings = true;
    for (auto it = arguments.begin() + 1;
         all_arguments_are_strings && it != arguments.end(); ++it) {
      all_arguments_are_strings = *it == VMType::String();
    }
    if (all_arguments_are_strings) {
      type_match_functions.push_back(candidate);
    } else if (arguments.size() == 2 &&
               arguments[1] ==
                   VMTypeMapper<std::vector<std::wstring>*>::vmtype) {
      function_vector = candidate;
    }
  }

  if (function_vector != nullptr) {
    output.function = function_vector;
    auto argument_values = std::make_unique<std::vector<std::wstring>>();
    for (auto it = output.tokens.begin() + 1; it != output.tokens.end(); ++it) {
      argument_values->push_back(it->value);
    }
    output.inputs.push_back(vm::NewConstantExpression(
        VMTypeMapper<std::unique_ptr<std::vector<std::wstring>>>::New(
            std::move(argument_values))));
  } else if (!type_match_functions.empty()) {
    // TODO: Choose the most suitable one given our arguments.
    output.function = type_match_functions[0];
    CHECK_GE(output.function->type.type_arguments.size(),
             1ul /* return type */);
    size_t expected_arguments = output.function->type.type_arguments.size() - 1;
    if (output.tokens.size() - 1 > expected_arguments) {
      return Error(L"Too many arguments given for `" + output.tokens[0].value +
                   L"` (expected: " + std::to_wstring(expected_arguments) +
                   L")");
    }

    for (auto it = output.tokens.begin() + 1; it != output.tokens.end(); ++it) {
      output.inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(it->value)));
    }

    while (output.inputs.size() < expected_arguments) {
      output.inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(L"")));
    }
  } else {
    return Error(L"No suitable definition found: " + output.tokens[0].value);
  }

  return Success(std::move(output));
}

ValueOrError<ParsedCommand> Parse(std::shared_ptr<LazyString> command,
                                  Environment* environment) {
  return Parse(command, environment, EmptyString(),
               {VMType::Void(), VMType::String()});
}

futures::ValueOrError<std::unique_ptr<Value>> Execute(
    std::shared_ptr<OpenBuffer> buffer, ParsedCommand parsed_command) {
  std::shared_ptr<Expression> expression = vm::NewFunctionCall(
      vm::NewConstantExpression(
          std::make_unique<vm::Value>(*parsed_command.function)),
      std::move(parsed_command.inputs));
  if (expression->Types().empty()) {
    // TODO: Show the error.
    return futures::Past(ValueOrError<std::unique_ptr<Value>>(
        Error(L"Unable to compile (type mismatch).")));
  }
  return futures::Transform(
      buffer->EvaluateExpression(expression.get()),
      [](std::unique_ptr<Value> value) { return Success(std::move(value)); });
}

futures::Value<EmptyValue> RunCppCommandShellHandler(
    const std::wstring& command, EditorState* editor_state) {
  return futures::Transform(RunCppCommandShell(command, editor_state),
                            futures::Past(EmptyValue()));
}

futures::Value<ColorizePromptOptions> ColorizeOptionsProvider(
    EditorState* editor, std::shared_ptr<LazyString> line) {
  ColorizePromptOptions output;
  auto buffer = editor->current_buffer();
  auto environment =
      (buffer == nullptr ? editor->environment() : buffer->environment()).get();
  if (auto parsed_command = Parse(line, environment);
      !parsed_command.IsError()) {
    output.tokens.push_back({{.value = L"",
                              .begin = ColumnNumber(0),
                              .end = ColumnNumber() + line->size()},
                             .modifiers = {LineModifier::CYAN}});
  }

  using BufferMapper = vm::VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>;
  futures::Future<ColorizePromptOptions> output_future;
  if (auto command = Parse(line, environment, NewLazyString(L"Preview"),
                           {BufferMapper::vmtype});
      buffer != nullptr && !command.IsError()) {
    Execute(buffer, std::move(command.value()))
        .SetConsumer(
            [consumer = output_future.consumer, buffer,
             output](ValueOrError<std::unique_ptr<vm::Value>> value) mutable {
              if (!value.IsError() &&
                  value.value()->type == BufferMapper::vmtype) {
                output.context = BufferMapper::get(value.value().get());
              }
              consumer(output);
            });
  } else {
    output_future.consumer(std::move(output));
  }
  return output_future.value;
}

class RunCppCommand : public Command {
 public:
  RunCppCommand(CppCommandMode mode) : mode_(mode) {}

  wstring Description() const override {
    switch (mode_) {
      case CppCommandMode::kLiteral:
        return L"prompts for a command (a C string) and runs it";
      case CppCommandMode::kShell:
        return L"prompts for a command, splits it into tokens, and runs it";
    }
    CHECK(false);
    return L"";
  }

  wstring Category() const override { return L"Extensions"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }

    PromptOptions options;
    options.editor_state = editor_state;
    std::wstring prompt;
    switch (mode_) {
      case CppCommandMode::kLiteral:
        options.handler = RunCppCommandLiteralHandler;
        prompt = L"cpp";
        break;
      case CppCommandMode::kShell:
        options.handler = RunCppCommandShellHandler;
        options.colorize_options_provider =
            [editor_state](const std::shared_ptr<LazyString>& line,
                           std::unique_ptr<ProgressChannel>,
                           std::shared_ptr<Notification>) {
              return ColorizeOptionsProvider(editor_state, line);
            };
        prompt = L":";
        break;
    }

    if (editor_state->structure() == StructureLine()) {
      editor_state->ResetStructure();
      options.handler(buffer->current_line()->ToString(), editor_state);
    } else {
      options.prompt = prompt + L" ";
      options.history_file = prompt == L":" ? L"colon" : prompt;
      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      options.status = PromptOptions::Status::kBuffer;
      Prompt(options);
    }
  }

 private:
  const CppCommandMode mode_;
};

}  // namespace

futures::Value<std::unique_ptr<vm::Value>> RunCppCommandShell(
    const std::wstring& command, EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(std::unique_ptr<vm::Value>());
  }
  buffer->ResetMode();

  auto parsed_command =
      Parse(NewLazyString(std::move(command)), buffer->environment().get());
  if (parsed_command.IsError()) {
    if (!parsed_command.error().description.empty()) {
      buffer->status()->SetWarningText(parsed_command.error().description);
    }
    return futures::Past(std::unique_ptr<vm::Value>());
  }

  futures::Future<std::unique_ptr<vm::Value>> output;
  Execute(buffer, std::move(parsed_command.value()))
      .SetConsumer([consumer = output.consumer,
                    buffer](ValueOrError<std::unique_ptr<vm::Value>> value) {
        if (value.IsError()) {
          buffer->status()->SetWarningText(value.error().description);
          consumer(nullptr);
        } else {
          consumer(std::move(value.value()));
        }
      });
  return output.value;
}

std::unique_ptr<Command> NewRunCppCommand(CppCommandMode mode) {
  return std::make_unique<RunCppCommand>(mode);
}
}  // namespace afc::editor
