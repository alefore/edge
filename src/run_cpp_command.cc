#include "src/run_cpp_command.h"

#include <memory>

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

namespace afc::editor {
namespace {

futures::Value<bool> RunCppCommandLiteralHandler(const wstring& name,
                                                 EditorState* editor_state) {
  // TODO(easy): Honor `multiple_buffers`.
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(true);
  }
  buffer->ResetMode();
  buffer->EvaluateString(name);
  return futures::Past(true);
}

struct Token {
  std::wstring value;
  ColumnNumber begin;
  // `end` is the first column that isn't part of the token.
  ColumnNumber end;
};

// Given: foo bar "hey there"
// Returns a vector with "foo", "bar", and "hey there".
std::vector<Token> SplitCommand(const LazyString& command) {
  std::vector<Token> output;
  Token token;
  auto push = [&](ColumnNumber end) {
    if (!token.value.empty()) {
      token.end = end;
      output.push_back(std::move(token));
    }
    token.value = L"";
    token.begin = ++end;
  };

  for (ColumnNumber i; i.ToDelta() < command.size(); ++i) {
    char c = command.get(i);
    if (c == ' ') {
      push(i);
    } else if (c == '\"') {
      ++i;
      while (i.ToDelta() < command.size() && command.get(i) != '\"') {
        token.value.push_back(command.get(i));
        ++i;
      }
    } else {
      token.value.push_back(c);
    }
  }
  push(ColumnNumber() + command.size());
  return output;
}

struct ParsedCommand {
  static ParsedCommand Error(std::wstring error) {
    ParsedCommand output;
    output.error = error;
    return output;
  }

  std::optional<std::wstring> error;
  std::vector<Token> tokens;
  vm::Value* function = nullptr;
  std::vector<std::unique_ptr<Expression>> inputs;
};

ParsedCommand Parse(const LazyString& command, Environment* environment) {
  ParsedCommand output;
  output.tokens = SplitCommand(command);
  if (output.tokens.empty()) {
    return ParsedCommand::Error(L"");  // No-op.
  }

  std::vector<Value*> functions;
  environment->CaseInsensitiveLookup(output.tokens[0].value, &functions);

  if (functions.empty()) {
    return ParsedCommand::Error(L"Unknown symbol: " + output.tokens[0].value);
  }

  // Filter functions that match our type expectations.
  std::vector<Value*> type_match_functions;
  Value* function_vector = nullptr;
  for (auto& candidate : functions) {
    if (!candidate->IsFunction()) {
      continue;
    }
    const auto& arguments = candidate->type.type_arguments;
    if (!(arguments[0] == VMType::Void())) {
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
      return ParsedCommand::Error(L"Too many arguments given for `" +
                                  output.tokens[0].value + L"` (expected: " +
                                  std::to_wstring(expected_arguments) + L")");
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
    return ParsedCommand::Error(L"No suitable definition found: " +
                                output.tokens[0].value);
  }

  return output;
}

futures::Value<bool> Execute(std::shared_ptr<OpenBuffer> buffer,
                             ParsedCommand parsed_command) {
  std::shared_ptr<Expression> expression = vm::NewFunctionCall(
      vm::NewConstantExpression(
          std::make_unique<vm::Value>(*parsed_command.function)),
      std::move(parsed_command.inputs));
  if (expression->Types().empty()) {
    // TODO: Show the error.
    buffer->status()->SetWarningText(L"Unable to compile (type mismatch).");
    return futures::Past(true);
  }
  return futures::ImmediateTransform(
      buffer->EvaluateExpression(expression.get()),
      [buffer](std::unique_ptr<Value>) { return true; });
}

futures::Value<bool> RunCppCommandShellHandler(const std::wstring& command,
                                               EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(true);
  }
  buffer->ResetMode();

  auto parsed_command =
      Parse(*NewLazyString(std::move(command)), buffer->environment().get());
  if (parsed_command.error.has_value()) {
    if (!parsed_command.error.value().empty()) {
      buffer->status()->SetWarningText(parsed_command.error.value());
    }
    return futures::Past(true);
  }

  return Execute(buffer, std::move(parsed_command));
}

futures::Value<bool> RunCppCommandShellChangeHandler(
    const std::shared_ptr<OpenBuffer>& prompt_buffer) {
  auto buffer = prompt_buffer->editor()->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(true);
  }
  auto line = prompt_buffer->LineAt(LineNumber(0));
  auto parsed_command = Parse(*line->contents(), buffer->environment().get());

  LineModifierSet modifiers;
  if (!parsed_command.error.has_value()) {
    modifiers.insert(LineModifier::CYAN);
  }

  Line::Options output;
  output.AppendString(line->contents(), std::move(modifiers));
  prompt_buffer->AppendRawLine(Line::New(std::move(output)));
  prompt_buffer->EraseLines(LineNumber(0), LineNumber(1));

  CHECK_EQ(prompt_buffer->lines_size(), LineNumberDelta(1));
  return futures::Past(true);
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
        options.change_handler = RunCppCommandShellChangeHandler;
        prompt = L":";
        break;
    }

    if (editor_state->structure() == StructureLine()) {
      editor_state->ResetStructure();
      options.handler(buffer->current_line()->ToString(), editor_state);
    } else {
      options.prompt = prompt + L" ";
      options.history_file = prompt;
      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      options.status = PromptOptions::Status::kBuffer;
      Prompt(options);
    }
  }

 private:
  const CppCommandMode mode_;
};

}  // namespace

std::unique_ptr<Command> NewRunCppCommand(CppCommandMode mode) {
  return std::make_unique<RunCppCommand>(mode);
}
}  // namespace afc::editor
