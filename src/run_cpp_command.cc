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
#include "src/language/overload.h"
#include "src/lazy_string_append.h"
#include "src/line_prompt_mode.h"
#include "src/substring.h"
#include "src/tests/tests.h"
#include "src/tokenize.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"

namespace afc::editor {
using concurrent::Notification;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::IgnoreErrors;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;

using vm::VMType;
using vm::VMTypeMapper;

namespace gc = language::gc;
namespace {

struct SearchNamespaces {
  SearchNamespaces(const OpenBuffer& buffer)
      : namespaces([&] {
          std::vector<vm::Namespace> output = {vm::Namespace({})};
          auto var = NewLazyString(
              buffer.Read(buffer_variables::cpp_prompt_namespaces));
          for (auto& token : TokenizeBySpaces(var.value())) {
            output.push_back(vm::Namespace({token.value}));
          }
          return output;
        }()) {}

  const std::vector<vm::Namespace> namespaces;
};

futures::Value<EmptyValue> RunCppCommandLiteralHandler(
    EditorState& editor_state, NonNull<std::shared_ptr<LazyString>> name) {
  // TODO(easy): Honor `multiple_buffers`.
  return VisitPointer(
      editor_state.current_buffer(),
      [&](gc::Root<OpenBuffer> buffer) {
        buffer.ptr()->ResetMode();
        // TODO(easy, 2022-06-05): Get rid of call to ToString.
        return buffer
            .ptr()

            ->EvaluateString(name->ToString())
            .Transform([buffer](gc::Root<vm::Value> value) {
              if (value.ptr()->IsVoid()) return Success();
              std::ostringstream oss;
              oss << "Evaluation result: " << value.ptr().value();
              buffer.ptr()->status().SetInformationText(
                  FromByteString(oss.str()));
              return Success();
            })
            .ConsumeErrors([](Error) { return futures::Past(EmptyValue()); });
      },
      [] { return futures::Past(EmptyValue()); });
}

struct ParsedCommand {
  std::vector<Token> tokens;
  gc::Root<vm::Value> function;
  std::vector<NonNull<std::unique_ptr<vm::Expression>>> function_inputs;
};

ValueOrError<ParsedCommand> Parse(
    gc::Pool& pool, NonNull<std::shared_ptr<LazyString>> command,
    vm::Environment& environment,
    NonNull<std::shared_ptr<LazyString>> function_name_prefix,
    std::unordered_set<VMType> accepted_return_types,
    const SearchNamespaces& search_namespaces) {
  std::vector<Token> output_tokens = TokenizeBySpaces(command.value());
  if (output_tokens.empty()) {
    // Deliberately empty so as to not trigger a status update.
    return Error(L"");
  }

  CHECK(!search_namespaces.namespaces.empty());
  std::vector<gc::Root<vm::Value>> functions;
  for (const auto& n : search_namespaces.namespaces) {
    environment.CaseInsensitiveLookup(
        n,
        StringAppend(function_name_prefix,
                     NewLazyString(output_tokens[0].value))
            ->ToString(),
        &functions);
    if (!functions.empty()) break;
  }

  if (functions.empty()) {
    return Error(L"Unknown symbol: " + output_tokens[0].value);
  }

  // Filter functions that match our type expectations.
  std::vector<gc::Root<vm::Value>> type_match_functions;
  std::optional<gc::Root<vm::Value>> function_vector;
  for (gc::Root<vm::Value>& candidate : functions) {
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
                   VMTypeMapper<NonNull<
                       std::shared_ptr<std::vector<std::wstring>>>>::vmtype) {
      function_vector = candidate;
    }
  }

  std::optional<gc::Root<vm::Value>> output_function;
  std::vector<NonNull<std::unique_ptr<vm::Expression>>> output_function_inputs;

  if (function_vector.has_value()) {
    output_function = function_vector.value();
    NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_values;
    for (auto it = output_tokens.begin() + 1; it != output_tokens.end(); ++it) {
      argument_values->push_back(it->value);
    }
    output_function_inputs.push_back(vm::NewConstantExpression(
        VMTypeMapper<NonNull<std::shared_ptr<std::vector<std::wstring>>>>::New(
            pool, std::move(argument_values))));
  } else if (!type_match_functions.empty()) {
    // TODO: Choose the most suitable one given our arguments.
    output_function = type_match_functions[0];
    CHECK_GE(output_function.value().ptr()->type.type_arguments.size(),
             1ul /* return type */);
    size_t expected_arguments =
        output_function.value().ptr()->type.type_arguments.size() - 1;
    if (output_tokens.size() - 1 > expected_arguments) {
      return Error(L"Too many arguments given for `" + output_tokens[0].value +
                   L"` (expected: " + std::to_wstring(expected_arguments) +
                   L")");
    }

    for (auto it = output_tokens.begin() + 1; it != output_tokens.end(); ++it) {
      output_function_inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, it->value)));
    }

    while (output_function_inputs.size() < expected_arguments) {
      output_function_inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, L"")));
    }
  } else {
    return Error(L"No suitable definition found: " + output_tokens[0].value);
  }
  return ParsedCommand{.tokens = std::move(output_tokens),
                       .function = output_function.value(),
                       .function_inputs = std::move(output_function_inputs)};
}

ValueOrError<ParsedCommand> Parse(gc::Pool& pool,
                                  NonNull<std::shared_ptr<LazyString>> command,
                                  vm::Environment& environment,
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
            gc::Root<OpenBuffer> buffer = NewBufferForTests();
            gc::Pool pool;
            vm::Environment environment;
            ValueOrError<ParsedCommand> output =
                Parse(pool, EmptyString(), environment, EmptyString(),
                      std::unordered_set<VMType>({VMType::String()}),
                      SearchNamespaces(buffer.ptr().value()));
            CHECK_EQ(std::get<Error>(output), Error(L""));
          }},
     {.name = L"NonEmptyCommandNoMatch",
      .callback =
          [] {
            gc::Root<OpenBuffer> buffer = NewBufferForTests();
            gc::Pool pool;
            vm::Environment environment;
            ValueOrError<ParsedCommand> output =
                Parse(pool, NewLazyString(L"foo"), environment, EmptyString(),
                      std::unordered_set<VMType>({VMType::String()}),
                      SearchNamespaces(buffer.ptr().value()));
            Error error = std::get<Error>(output);
            LOG(INFO) << "Error: " << error;
            CHECK_GT(error.read().size(), sizeof("Unknown "));
            CHECK(error.read().substr(0, sizeof("Unknown ") - 1) ==
                  L"Unknown ");
          }},
     {.name = L"CommandMatch", .callback = [] {
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        gc::Pool pool;
        vm::Environment environment;
        environment.Define(L"foo", vm::Value::NewString(pool, L"bar"));
        ValueOrError<ParsedCommand> output =
            Parse(pool, NewLazyString(L"foo"), environment, EmptyString(),
                  std::unordered_set<VMType>({VMType::String()}),
                  SearchNamespaces(buffer.ptr().value()));
        CHECK(std::holds_alternative<Error>(output));
      }}});
}

futures::ValueOrError<gc::Root<vm::Value>> Execute(
    OpenBuffer& buffer, ParsedCommand parsed_command) {
  NonNull<std::unique_ptr<vm::Expression>> expression =
      vm::NewFunctionCall(vm::NewConstantExpression(parsed_command.function),
                          std::move(parsed_command.function_inputs));
  if (expression->Types().empty()) {
    // TODO: Show the error.
    return futures::Past(Error(L"Unable to compile (type mismatch)."));
  }
  return buffer.EvaluateExpression(expression.value(),
                                   buffer.environment().ToRoot());
}

futures::Value<EmptyValue> RunCppCommandShellHandler(
    NonNull<EditorState*> editor_state,
    NonNull<std::shared_ptr<LazyString>> command) {
  // TODO(easy, 2022-06-05): Get rid of ToString.
  return RunCppCommandShell(command->ToString(), editor_state.value())
      .Transform([](auto) { return Success(); })
      .ConsumeErrors([](auto) { return futures::Past(EmptyValue()); });
}

futures::Value<ColorizePromptOptions> ColorizeOptionsProvider(
    EditorState& editor, NonNull<std::shared_ptr<LazyString>> line,
    const SearchNamespaces& search_namespaces) {
  ColorizePromptOptions output;
  std::optional<gc::Root<OpenBuffer>> buffer = editor.current_buffer();
  vm::Environment& environment =
      (buffer.has_value() ? buffer->ptr()->environment()
                          : editor.environment().ptr())
          .value();

  std::visit(overload{IgnoreErrors{},
                      [&](ParsedCommand) {
                        output.tokens.push_back(
                            {.token = {.value = L"",
                                       .begin = ColumnNumber(0),
                                       .end = ColumnNumber() + line->size()},
                             .modifiers = {LineModifier::CYAN}});
                      }},
             Parse(editor.gc_pool(), line, environment, search_namespaces));

  using BufferMapper = vm::VMTypeMapper<gc::Root<editor::OpenBuffer>>;
  futures::Future<ColorizePromptOptions> output_future;
  std::visit(
      overload{[&](Error) { output_future.consumer(std::move(output)); },
               [&](ParsedCommand command) {
                 Execute(buffer->ptr().value(), std::move(command))
                     .SetConsumer([consumer = output_future.consumer, buffer,
                                   output](ValueOrError<gc::Root<vm::Value>>
                                               value_or_error) mutable {
                       std::visit(overload{IgnoreErrors{},
                                           [&](gc::Root<vm::Value> value) {
                                             if (value.ptr()->type ==
                                                 BufferMapper::vmtype) {
                                               output.context =
                                                   BufferMapper::get(
                                                       value.ptr().value());
                                             }
                                           }},
                                  std::move(value_or_error));
                       consumer(output);
                     });
               }},
      buffer.has_value()
          ? ValueOrError<ParsedCommand>(Error(L"Buffer has no value"))
          : Parse(editor.gc_pool(), line, environment,
                  NewLazyString(L"Preview"), {BufferMapper::vmtype},
                  search_namespaces));
  return std::move(output_future.value);
}

}  // namespace

futures::ValueOrError<gc::Root<vm::Value>> RunCppCommandShell(
    const std::wstring& command, EditorState& editor_state) {
  using futures::Past;
  auto buffer = editor_state.current_buffer();
  if (!buffer.has_value()) {
    return Past(ValueOrError<gc::Root<vm::Value>>(Error(L"No active buffer.")));
  }
  buffer->ptr()->ResetMode();

  SearchNamespaces search_namespaces(buffer->ptr().value());
  return std::visit(
      overload{[&](Error error) {
                 if (!error.read().empty()) {
                   buffer->ptr()->status().Set(error);
                 }
                 return Past(ValueOrError<gc::Root<vm::Value>>(
                     Error(L"Unable to parse command")));
               },
               [&](ParsedCommand parsed_command) {
                 return futures::OnError(
                     Execute(buffer->ptr().value(), std::move(parsed_command)),
                     [buffer](Error error) {
                       buffer->ptr()->status().Set(error);
                       return futures::Past(error);
                     });
               }},
      Parse(editor_state.gc_pool(), NewLazyString(std::move(command)),
            buffer->ptr()->environment().value(), search_namespaces));
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
        std::function<futures::Value<EmptyValue>(
            NonNull<std::shared_ptr<LazyString>> input)>
            handler;
        PromptOptions::ColorizeFunction colorize_options_provider;
        switch (mode) {
          case CppCommandMode::kLiteral:
            handler = std::bind_front(RunCppCommandLiteralHandler,
                                      std::ref(editor_state));
            prompt = L"cpp";
            break;
          case CppCommandMode::kShell:
            handler =
                std::bind_front(RunCppCommandShellHandler,
                                NonNull<EditorState*>::AddressOf(editor_state));
            prompt = L":";
            auto buffer = editor_state.current_buffer();
            CHECK(buffer.has_value());
            colorize_options_provider =
                [&editor_state,
                 search_namespaces = SearchNamespaces(buffer->ptr().value())](
                    const NonNull<std::shared_ptr<LazyString>>& line,
                    NonNull<std::unique_ptr<ProgressChannel>>,
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
