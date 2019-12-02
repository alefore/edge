#include "src/run_cpp_command.h"

#include <memory>

#include "src/buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"

namespace afc {
namespace editor {

namespace {

void RunCppCommandLiteralHandler(const wstring& name,
                                 EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  buffer->ResetMode();
  buffer->EvaluateString(name, [](std::unique_ptr<Value>) { /* Nothing. */ });
}

// Given: foo bar "hey there"
// Returns a vector with "foo", "bar", and "hey there".
std::vector<std::wstring> SplitCommand(const std::wstring& command) {
  std::vector<std::wstring> output;
  std::wstring token;
  auto push = [&]() {
    if (!token.empty()) {
      output.push_back(token);
    }
    token = L"";
  };

  for (size_t i = 0; i < command.length(); i++) {
    char c = command[i];
    if (c == ' ') {
      push();
    } else if (c == '\"') {
      i++;
      while (i < command.length() && command[i] != '\"') {
        token.push_back(command[i]);
        i++;
      }
    } else {
      token.push_back(command[i]);
    }
  }
  push();
  return output;
}

void Execute(std::shared_ptr<OpenBuffer> buffer, vm::Value* callback,

             std::vector<std::wstring> inputs) {
  std::vector<std::unique_ptr<Expression>> args;
  for (size_t i = 1; i < inputs.size(); i++) {
    args.push_back(
        vm::NewConstantExpression(vm::Value::NewString(std::move(inputs[i]))));
  }
  CHECK_GE(callback->type.type_arguments.size(), 1);  // Skip the return type.
  size_t expected_arguments = callback->type.type_arguments.size() - 1;
  if (args.size() > expected_arguments) {
    buffer->status()->SetWarningText(
        L"Too many arguments given for `" + inputs[0] + L"` (expected: " +
        std::to_wstring(expected_arguments) + L")");
    return;
  }
  while (args.size() < expected_arguments) {
    args.push_back(vm::NewConstantExpression(vm::Value::NewString(L"")));
  }

  std::shared_ptr<Expression> expression = vm::NewFunctionCall(
      vm::NewConstantExpression(std::make_unique<vm::Value>(*callback)),
      std::move(args));
  if (expression->Types().empty()) {
    // TODO: Show the error.
    buffer->status()->SetWarningText(L"Unable to compile.");
    return;
  }
  buffer->EvaluateExpression(
      expression.get(), [buffer, expression](Value::Ptr) { /* Nothing. */ });
}

void RunCppCommandShellHandler(const std::wstring& command,
                               EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  buffer->ResetMode();

  std::vector<wstring> tokens = SplitCommand(command);
  if (tokens.empty()) {
    return;
  }
  std::vector<Value*> functions;
  buffer->environment()->CaseInsensitiveLookup(tokens[0], &functions);

  if (functions.empty()) {
    buffer->status()->SetWarningText(L"Unknown symbol: " + tokens[0]);
  }

  // Filter those that match our type expectations.
  std::vector<Value*> type_match_functions;
  for (auto& candidate : functions) {
    if (!candidate->IsFunction()) {
      continue;
    }
    for (auto& arg_type : candidate->type.type_arguments) {
      if (!(arg_type == VMType::String())) {
        continue;
      }
    }
    type_match_functions.push_back(candidate);
  }
  if (type_match_functions.empty()) {
    buffer->status()->SetWarningText(L"No suitable definition found: " +
                                     tokens[0]);
    return;
  }
  // TODO: Pick the most suitable (based on the number of arguments given).
  Execute(buffer, type_match_functions[0], tokens);
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

    std::function<void(const std::wstring&, EditorState*)> handler;
    std::wstring prompt;
    switch (mode_) {
      case CppCommandMode::kLiteral:
        handler = RunCppCommandLiteralHandler;
        prompt = L"cpp";
        break;
      case CppCommandMode::kShell:
        handler = RunCppCommandShellHandler;
        prompt = L":";
        break;
    }

    if (editor_state->structure() == StructureLine()) {
      editor_state->ResetStructure();
      handler(buffer->current_line()->ToString(), editor_state);
    } else {
      PromptOptions options;
      options.prompt = prompt + L" ";
      options.history_file = prompt;
      options.handler = handler;

      options.cancel_handler = [](EditorState*) { /* Nothing. */ };
      options.status = PromptOptions::Status::kBuffer;
      Prompt(editor_state, options);
    }
  }

 private:
  const CppCommandMode mode_;
};

}  // namespace

std::unique_ptr<Command> NewRunCppCommand(CppCommandMode mode) {
  return std::make_unique<RunCppCommand>(mode);
}

}  // namespace editor
}  // namespace afc
