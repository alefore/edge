#include "src/run_cpp_command.h"

#include <memory>
#include <sstream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
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

struct SearchNamespaces {
  SearchNamespaces(const OpenBuffer& buffer)
      : namespaces([&] {
          std::vector<Environment::Namespace> output(1);
          auto var = NewLazyString(
              buffer.Read(buffer_variables::cpp_prompt_namespaces));
          for (auto& token : TokenizeBySpaces(*var)) {
            output.push_back({token.value});
          }
          return output;
        }()) {}

  const std::vector<Environment::Namespace> namespaces;
};

futures::Value<EmptyValue> RunCppCommandLiteralHandler(
    const wstring& name, EditorState* editor_state) {
  // TODO(easy): Honor `multiple_buffers`.
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return futures::Past(EmptyValue());
  }
  buffer->ResetMode();
  return buffer->EvaluateString(name)
      .Transform([buffer](std::unique_ptr<Value> value) {
        CHECK(value != nullptr);
        if (value->IsVoid()) return Success();
        std::ostringstream oss;
        CHECK(value != nullptr);
        oss << "Evaluation result: " << *value;
        buffer->status()->SetInformationText(FromByteString(oss.str()));
        return Success();
      })
      .ConsumeErrors([](Error) { return futures::Past(EmptyValue()); });
}

struct ParsedCommand {
  std::vector<Token> tokens;
  vm::Value* function = nullptr;
  std::vector<std::unique_ptr<Expression>> inputs;
};

ValueOrError<ParsedCommand> Parse(
    std::shared_ptr<LazyString> command, Environment* environment,
    std::shared_ptr<LazyString> function_name_prefix,
    std::unordered_set<VMType> accepted_return_types,
    const SearchNamespaces& search_namespaces) {
  ParsedCommand output;
  output.tokens = TokenizeBySpaces(*command);
  if (output.tokens.empty()) {
    // Deliberately empty so as to not trigger a status update.
    return Error(L"");
  }

  CHECK(!search_namespaces.namespaces.empty());
  std::vector<Value*> functions;
  for (const auto& n : search_namespaces.namespaces) {
    environment->CaseInsensitiveLookup(
        n,
        StringAppend(function_name_prefix,
                     NewLazyString(output.tokens[0].value))
            ->ToString(),
        &functions);
    if (!functions.empty()) break;
  }

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
                                  Environment* environment,
                                  const SearchNamespaces& search_namespaces) {
  return Parse(command, environment, EmptyString(),
               {VMType::Void(), VMType::String()}, search_namespaces);
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
  return buffer->EvaluateExpression(expression.get(), buffer->environment())
      .Transform([](std::unique_ptr<Value> value) {
        return Success(std::move(value));
      });
}

futures::Value<EmptyValue> RunCppCommandShellHandler(
    const std::wstring& command, EditorState* editor_state) {
  return RunCppCommandShell(command, editor_state).Transform([](auto) {
    return EmptyValue();
  });
}

futures::Value<ColorizePromptOptions> ColorizeOptionsProvider(
    EditorState* editor, std::shared_ptr<LazyString> line,
    const SearchNamespaces& search_namespaces) {
  ColorizePromptOptions output;
  auto buffer = editor->current_buffer();
  auto environment =
      (buffer == nullptr ? editor->environment() : buffer->environment()).get();
  if (auto parsed_command = Parse(line, environment, search_namespaces);
      !parsed_command.IsError()) {
    output.tokens.push_back({{.value = L"",
                              .begin = ColumnNumber(0),
                              .end = ColumnNumber() + line->size()},
                             .modifiers = {LineModifier::CYAN}});
  }

  using BufferMapper = vm::VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>;
  futures::Future<ColorizePromptOptions> output_future;
  if (auto command = Parse(line, environment, NewLazyString(L"Preview"),
                           {BufferMapper::vmtype}, search_namespaces);
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
  return std::move(output_future.value);
}

}  // namespace

futures::Value<std::unique_ptr<vm::Value>> RunCppCommandShell(
    const std::wstring& command, EditorState* editor_state) {
  using futures::Past;
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return Past(std::unique_ptr<vm::Value>());
  }
  buffer->ResetMode();

  SearchNamespaces search_namespaces(*buffer);
  auto parsed_command = Parse(NewLazyString(std::move(command)),
                              buffer->environment().get(), search_namespaces);
  if (parsed_command.IsError()) {
    if (!parsed_command.error().description.empty()) {
      buffer->status()->SetWarningText(parsed_command.error().description);
    }
    return Past(std::unique_ptr<vm::Value>());
  }

  return Execute(buffer, std::move(parsed_command.value()))
      .ConsumeErrors([buffer](Error error) {
        buffer->status()->SetWarningText(error.description);
        return Past(std::unique_ptr<vm::Value>());
      });
}

std::unique_ptr<Command> NewRunCppCommand(EditorState& editor_state,
                                          CppCommandMode mode) {
  std::wstring description;
  switch (mode) {
    case CppCommandMode::kLiteral:
      description = L"prompts for a command (a C string) and runs it";
      break;
    case CppCommandMode::kShell:
      description =
          L"prompts for a command, splits it into tokens, and runs it";
      break;
  }
  CHECK(!description.empty());
  return NewLinePromptCommand(
      editor_state, description, [mode](EditorState* editor_state) {
        PromptOptions options;
        auto buffer = editor_state->current_buffer();
        CHECK(buffer != nullptr);
        options.editor_state = editor_state;
        std::wstring prompt;
        switch (mode) {
          case CppCommandMode::kLiteral:
            options.handler = RunCppCommandLiteralHandler;
            prompt = L"cpp";
            break;
          case CppCommandMode::kShell:
            options.handler = RunCppCommandShellHandler;
            SearchNamespaces search_namespaces(*buffer);
            options.colorize_options_provider =
                [editor_state, search_namespaces](
                    const std::shared_ptr<LazyString>& line,
                    std::unique_ptr<ProgressChannel>,
                    std::shared_ptr<Notification>) {
                  return ColorizeOptionsProvider(editor_state, line,
                                                 search_namespaces);
                };
            prompt = L":";
            break;
        }

        options.prompt = prompt + L" ";
        options.history_file = prompt == L":" ? L"colon" : prompt;
        options.cancel_handler = [](EditorState*) { /* Nothing. */ };
        options.status = PromptOptions::Status::kBuffer;
        return options;
      });
}
}  // namespace afc::editor
