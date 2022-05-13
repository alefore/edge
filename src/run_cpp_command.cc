#include "src/run_cpp_command.h"

#include <functional>
#include <memory>
#include <sstream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/line_prompt_mode.h"
#include "src/substring.h"
#include "src/tests/tests.h"
#include "src/tokenize.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

namespace afc::editor {
using concurrent::Notification;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;
namespace gc = language::gc;
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
    const wstring& name, EditorState& editor_state) {
  // TODO(easy): Honor `multiple_buffers`.
  auto buffer = editor_state.current_buffer();
  if (buffer == nullptr) {
    return futures::Past(EmptyValue());
  }
  buffer->ResetMode();
  return buffer->EvaluateString(name)
      .Transform([buffer](gc::Root<Value> value) {
        if (value.ptr()->IsVoid()) return Success();
        std::ostringstream oss;
        oss << "Evaluation result: " << value.ptr().value();
        buffer->status().SetInformationText(FromByteString(oss.str()));
        return Success();
      })
      .ConsumeErrors([](Error) { return futures::Past(EmptyValue()); });
}

struct ParsedCommand {
  std::vector<Token> tokens;
  // TODO(easy, 2022-05-12): Remove the optional here?
  std::optional<gc::Root<vm::Value>> function;
  std::vector<NonNull<std::unique_ptr<Expression>>> inputs;
};

ValueOrError<ParsedCommand> Parse(
    gc::Pool& pool, NonNull<std::shared_ptr<LazyString>> command,
    Environment& environment,
    NonNull<std::shared_ptr<LazyString>> function_name_prefix,
    std::unordered_set<VMType> accepted_return_types,
    const SearchNamespaces& search_namespaces) {
  ParsedCommand output;
  output.tokens = TokenizeBySpaces(*command);
  if (output.tokens.empty()) {
    // Deliberately empty so as to not trigger a status update.
    return Error(L"");
  }

  CHECK(!search_namespaces.namespaces.empty());
  std::vector<gc::Root<Value>> functions;
  for (const auto& n : search_namespaces.namespaces) {
    environment.CaseInsensitiveLookup(
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
  std::vector<gc::Root<Value>> type_match_functions;
  std::optional<gc::Root<Value>> function_vector;
  for (gc::Root<Value>& candidate : functions) {
    if (!candidate.ptr()->IsFunction()) {
      continue;
    }
    const auto& arguments = candidate.ptr()->type.type_arguments;
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

  if (function_vector.has_value()) {
    output.function = function_vector.value();
    auto argument_values = std::make_unique<std::vector<std::wstring>>();
    for (auto it = output.tokens.begin() + 1; it != output.tokens.end(); ++it) {
      argument_values->push_back(it->value);
    }
    output.inputs.push_back(vm::NewConstantExpression(
        VMTypeMapper<std::unique_ptr<std::vector<std::wstring>>>::New(
            pool, std::move(argument_values))));
  } else if (!type_match_functions.empty()) {
    // TODO: Choose the most suitable one given our arguments.
    output.function = type_match_functions[0];
    CHECK_GE(output.function.value().ptr()->type.type_arguments.size(),
             1ul /* return type */);
    size_t expected_arguments =
        output.function.value().ptr()->type.type_arguments.size() - 1;
    if (output.tokens.size() - 1 > expected_arguments) {
      return Error(L"Too many arguments given for `" + output.tokens[0].value +
                   L"` (expected: " + std::to_wstring(expected_arguments) +
                   L")");
    }

    for (auto it = output.tokens.begin() + 1; it != output.tokens.end(); ++it) {
      output.inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, it->value)));
    }

    while (output.inputs.size() < expected_arguments) {
      output.inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, L"")));
    }
  } else {
    return Error(L"No suitable definition found: " + output.tokens[0].value);
  }

  return output;
}

ValueOrError<ParsedCommand> Parse(gc::Pool& pool,
                                  NonNull<std::shared_ptr<LazyString>> command,
                                  Environment& environment,
                                  const SearchNamespaces& search_namespaces) {
  return Parse(pool, std::move(command), environment, EmptyString(),
               {VMType::Void(), VMType::String()}, search_namespaces);
}

namespace {
bool tests_parse_registration = tests::Register(
    L"RunCppCommand::Parse",
    {{.name = L"EmptyCommand",
      .callback =
          [] {
            NonNull<std::shared_ptr<OpenBuffer>> buffer = NewBufferForTests();
            gc::Pool pool;
            vm::Environment environment;
            auto output = Parse(pool, EmptyString(), environment, EmptyString(),
                                std::unordered_set<VMType>({VMType::String()}),
                                SearchNamespaces(buffer.value()));
            CHECK(output.IsError());
            CHECK(output.error().description.empty());
          }},
     {.name = L"NonEmptyCommandNoMatch",
      .callback =
          [] {
            NonNull<std::shared_ptr<OpenBuffer>> buffer = NewBufferForTests();
            gc::Pool pool;
            vm::Environment environment;
            auto output =
                Parse(pool, NewLazyString(L"foo"), environment, EmptyString(),
                      std::unordered_set<VMType>({VMType::String()}),
                      SearchNamespaces(buffer.value()));
            CHECK(output.IsError());
            LOG(INFO) << "Error: " << output.error();
            CHECK_GT(output.error().description.size(), sizeof("Unknown "));
            CHECK(output.error().description.substr(
                      0, sizeof("Unknown ") - 1) == L"Unknown ");
          }},
     {.name = L"CommandMatch", .callback = [] {
        NonNull<std::shared_ptr<OpenBuffer>> buffer = NewBufferForTests();
        gc::Pool pool;
        vm::Environment environment;
        environment.Define(L"foo", Value::NewString(pool, L"bar"));
        auto output =
            Parse(pool, NewLazyString(L"foo"), environment, EmptyString(),
                  std::unordered_set<VMType>({VMType::String()}),
                  SearchNamespaces(buffer.value()));
        CHECK(output.IsError());
      }}});
}

futures::ValueOrError<gc::Root<Value>> Execute(
    std::shared_ptr<OpenBuffer> buffer, ParsedCommand parsed_command) {
  CHECK(parsed_command.function.has_value());
  NonNull<std::unique_ptr<Expression>> expression =
      vm::NewFunctionCall(vm::NewConstantExpression(*parsed_command.function),
                          std::move(parsed_command.inputs));
  if (expression->Types().empty()) {
    // TODO: Show the error.
    return futures::Past(Error(L"Unable to compile (type mismatch)."));
  }
  return buffer->EvaluateExpression(*expression, buffer->environment());
}

futures::Value<EmptyValue> RunCppCommandShellHandler(
    const std::wstring& command, EditorState& editor_state) {
  return RunCppCommandShell(command, editor_state)
      .Transform([](auto) { return Success(); })
      .ConsumeErrors([](auto) { return futures::Past(EmptyValue()); });
}

futures::Value<ColorizePromptOptions> ColorizeOptionsProvider(
    EditorState& editor, NonNull<std::shared_ptr<LazyString>> line,
    const SearchNamespaces& search_namespaces) {
  ColorizePromptOptions output;
  auto buffer = editor.current_buffer();
  gc::Root<Environment> environment =
      (buffer == nullptr ? editor.environment() : buffer->environment());
  if (auto parsed_command = Parse(editor.gc_pool(), line,
                                  environment.ptr().value(), search_namespaces);
      !parsed_command.IsError()) {
    output.tokens.push_back({.token = {.value = L"",
                                       .begin = ColumnNumber(0),
                                       .end = ColumnNumber() + line->size()},
                             .modifiers = {LineModifier::CYAN}});
  }

  using BufferMapper = vm::VMTypeMapper<std::shared_ptr<editor::OpenBuffer>>;
  futures::Future<ColorizePromptOptions> output_future;
  if (auto command = Parse(editor.gc_pool(), line, environment.ptr().value(),
                           NewLazyString(L"Preview"), {BufferMapper::vmtype},
                           search_namespaces);
      buffer != nullptr && !command.IsError()) {
    Execute(buffer, std::move(command.value()))
        .SetConsumer([consumer = output_future.consumer, buffer,
                      output](ValueOrError<gc::Root<vm::Value>> value) mutable {
          if (!value.IsError() &&
              value.value().ptr()->type == BufferMapper::vmtype) {
            output.context = BufferMapper::get(value.value().ptr().value());
          }
          consumer(output);
        });
  } else {
    output_future.consumer(std::move(output));
  }
  return std::move(output_future.value);
}

}  // namespace

futures::ValueOrError<gc::Root<vm::Value>> RunCppCommandShell(
    const std::wstring& command, EditorState& editor_state) {
  using futures::Past;
  auto buffer = editor_state.current_buffer();
  if (buffer == nullptr) {
    return Past(ValueOrError<gc::Root<vm::Value>>(Error(L"No active buffer.")));
  }
  buffer->ResetMode();

  SearchNamespaces search_namespaces(*buffer);
  auto parsed_command =
      Parse(editor_state.gc_pool(), NewLazyString(std::move(command)),
            buffer->environment().ptr().value(), search_namespaces);
  if (parsed_command.IsError()) {
    if (!parsed_command.error().description.empty()) {
      buffer->status().SetWarningText(parsed_command.error().description);
    }
    return Past(
        ValueOrError<gc::Root<vm::Value>>(Error(L"Unable to parse command")));
  }

  return futures::OnError(Execute(buffer, std::move(parsed_command.value())),
                          [buffer](Error error) {
                            buffer->status().SetWarningText(error.description);
                            return futures::Past(error);
                          });
}

NonNull<std::unique_ptr<Command>> NewRunCppCommand(EditorState& editor_state,
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
      editor_state, description, [&editor_state, mode]() {
        std::wstring prompt;
        std::function<futures::Value<EmptyValue>(const wstring& input)> handler;
        PromptOptions::ColorizeFunction colorize_options_provider;
        switch (mode) {
          case CppCommandMode::kLiteral:
            // TODO(c++20): Use std::bind_front.
            handler = [&editor_state](const std::wstring& input) {
              return RunCppCommandLiteralHandler(input, editor_state);
            };
            prompt = L"cpp";
            break;
          case CppCommandMode::kShell:
            handler = [&editor_state](const std::wstring& input) {
              return RunCppCommandShellHandler(input, editor_state);
            };
            prompt = L":";
            auto buffer = editor_state.current_buffer();
            CHECK(buffer != nullptr);
            colorize_options_provider =
                [&editor_state, search_namespaces = SearchNamespaces(*buffer)](
                    const NonNull<std::shared_ptr<LazyString>>& line,
                    std::unique_ptr<ProgressChannel>,
                    NonNull<std::shared_ptr<Notification>>) {
                  return ColorizeOptionsProvider(editor_state, line,
                                                 search_namespaces);
                };
            break;
        }
        return PromptOptions{
            .editor_state = editor_state,
            .prompt = prompt + L" ",
            .history_file =
                prompt == L":" ? HistoryFile(L"colon") : HistoryFile(prompt),
            .colorize_options_provider = std::move(colorize_options_provider),
            .handler = std::move(handler),
            .cancel_handler = []() { /* Nothing. */ },
            .status = PromptOptions::Status::kBuffer};
      });
}
}  // namespace afc::editor
