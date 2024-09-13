#include "src/run_cpp_command.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_vm.h"
#include "src/command.h"
#include "src/concurrent/protected.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/line_prompt_mode.h"
#include "src/tests/tests.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"
#include "src/vm/natural.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::Protected;
using afc::concurrent::VersionPropertyKey;
using afc::futures::DeleteNotification;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::vm::Identifier;
using afc::vm::TypesToString;
using afc::vm::VMTypeMapper;

namespace afc::editor {
namespace {

struct SearchNamespaces {
  SearchNamespaces(const OpenBuffer& buffer)
      : namespaces([&] {
          static const vm::Namespace kEmptyNamespace;
          std::vector<vm::Namespace> output = {kEmptyNamespace};
          std::ranges::copy(
              TokenizeBySpaces(buffer.ReadLazyString(
                  buffer_variables::cpp_prompt_namespaces)) |
                  std::views::transform([](Token token) {
                    return vm::Namespace{{Identifier{token.value}}};
                  }),
              std::back_inserter(output));
          return output;
        }()) {}

  const std::vector<vm::Namespace> namespaces;
};

futures::Value<EmptyValue> RunCppCommandLiteralHandler(
    EditorState& editor_state, SingleLine name) {
  // TODO(easy): Honor `multiple_buffers`.
  return VisitPointer(
      editor_state.current_buffer(),
      [&](gc::Root<OpenBuffer> buffer) {
        buffer.ptr()->ResetMode();
        return buffer.ptr()
            ->EvaluateString(name.read())
            .Transform([buffer](gc::Root<vm::Value> value) {
              if (value.ptr()->IsVoid()) return Success();
              std::ostringstream oss;
              oss << "Evaluation result: " << value.ptr().value();
              buffer.ptr()->status().SetInformationText(
                  Line{SingleLine{LazyString{FromByteString(oss.str())}}});
              return Success();
            })
            .ConsumeErrors([](Error) { return futures::Past(EmptyValue()); });
      },
      [] { return futures::Past(EmptyValue()); });
}

struct ParsedCommand {
  std::vector<Token> tokens;
  // Should be a function of zero arguments.
  NonNull<std::shared_ptr<vm::Expression>> expression;
};

ValueOrError<ParsedCommand> Parse(
    gc::Pool& pool, LazyString command, const vm::Environment& environment,
    LazyString function_name_prefix,
    std::unordered_set<vm::Type> accepted_return_types,
    const SearchNamespaces& search_namespaces) {
  std::vector<Token> output_tokens = TokenizeBySpaces(command);
  if (ValueOrError<NonNull<std::shared_ptr<vm::Expression>>> parse =
          vm::natural::Compile(command, function_name_prefix, environment,
                               search_namespaces.namespaces, pool);
      !IsError(parse)) {
    LOG(INFO) << "Parse natural command: " << command;
    return ParsedCommand{.tokens = std::move(output_tokens),
                         .expression = ValueOrDie(std::move(parse))};
  }

  if (output_tokens.empty()) {
    // Deliberately empty so as to not trigger a status update.
    return Error{LazyString{}};
  }

  CHECK(!search_namespaces.namespaces.empty());
  std::vector<gc::Root<vm::Value>> functions;
  for (const auto& n : search_namespaces.namespaces) {
    environment.CaseInsensitiveLookup(n,
                                      Identifier{function_name_prefix.Append(
                                          LazyString{output_tokens[0].value})},
                                      &functions);
    if (!functions.empty()) break;
  }

  if (functions.empty()) {
    Error error{LazyString{L"Unknown symbol: "} + function_name_prefix +
                output_tokens[0].value};
    VLOG(5) << "Parse: " << error;
    return error;
  }

  // Filter functions that match our type expectations.
  std::vector<gc::Root<vm::Value>> type_match_functions;
  std::optional<gc::Root<vm::Value>> function_vector;
  std::vector<vm::Type> all_types_found;
  for (gc::Root<vm::Value>& candidate : functions) {
    if (!candidate.ptr()->IsFunction()) {
      continue;
    }
    all_types_found.push_back(candidate.ptr()->type);
    const vm::types::Function* function_type =
        std::get_if<vm::types::Function>(&candidate.ptr()->type);
    if (function_type == nullptr ||
        accepted_return_types.find(function_type->output.get()) ==
            accepted_return_types.end()) {
      continue;
    }

    if (std::all_of(function_type->inputs.begin(), function_type->inputs.end(),
                    [](const vm::Type& t) {
                      return std::holds_alternative<vm::types::String>(t);
                    })) {
      type_match_functions.push_back(candidate);
    } else if (function_type->inputs.size() == 1 &&
               function_type->inputs[0] ==
                   vm::GetVMType<NonNull<std::shared_ptr<
                       Protected<std::vector<std::wstring>>>>>::vmtype()) {
      function_vector = candidate;
    }
  }

  std::optional<gc::Root<vm::Value>> output_function;
  std::vector<NonNull<std::shared_ptr<vm::Expression>>> output_function_inputs;

  if (function_vector.has_value()) {
    output_function = function_vector.value();
    // TODO(2024-01-02, trivial): Convert to vector<LazyString>.
    auto argument_values =
        MakeNonNullShared<Protected<std::vector<std::wstring>>>(
            container::MaterializeVector(output_tokens | std::views::drop(1) |
                                         std::views::transform([](auto& v) {
                                           return v.value.ToString();
                                         })));

    output_function_inputs.push_back(
        vm::NewConstantExpression(VMTypeMapper<decltype(argument_values)>::New(
            pool, std::move(argument_values))));
  } else if (!type_match_functions.empty()) {
    // TODO: Choose the most suitable one given our arguments.
    output_function = type_match_functions[0];
    const vm::types::Function& function_type =
        std::get<vm::types::Function>(output_function.value().ptr()->type);
    size_t expected_arguments = function_type.inputs.size();
    if (output_tokens.size() - 1 > expected_arguments) {
      return Error{LazyString{L"Too many arguments given for `"} +
                   output_tokens[0].value + LazyString{L"` (expected: "} +
                   LazyString{std::to_wstring(expected_arguments)} +
                   LazyString{L")"}};
    }

    for (auto it = output_tokens.begin() + 1; it != output_tokens.end(); ++it) {
      output_function_inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, it->value)));
    }

    while (output_function_inputs.size() < expected_arguments) {
      output_function_inputs.push_back(
          vm::NewConstantExpression(vm::Value::NewString(pool, LazyString{})));
    }
  } else if (!all_types_found.empty()) {
    return Error{LazyString{L"Incompatible type found: "} +
                 output_tokens[0].value + LazyString{L": "} +
                 TypesToString(all_types_found)};
  } else {
    return Error{LazyString{L"No definition found: "} + output_tokens[0].value};
  }
  return ParsedCommand{
      .tokens = std::move(output_tokens),
      .expression = NewFunctionCall(
          NewConstantExpression(std::move(output_function.value())),
          std::move(output_function_inputs))};
}

ValueOrError<ParsedCommand> Parse(gc::Pool& pool, LazyString command,
                                  vm::Environment& environment,
                                  const SearchNamespaces& search_namespaces) {
  return Parse(pool, std::move(command), environment, LazyString{},
               {vm::types::Void{}, vm::types::String{}}, search_namespaces);
}

namespace {
bool tests_parse_registration = tests::Register(
    L"RunCppCommand::Parse",
    {{.name = L"EmptyCommand",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
            gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
            gc::Pool pool({});
            gc::Root<vm::Environment> environment = vm::Environment::New(pool);
            ValueOrError<ParsedCommand> output = Parse(
                pool, LazyString(), environment.ptr().value(), LazyString(),
                std::unordered_set<vm::Type>({vm::types::String{}}),
                SearchNamespaces(buffer.ptr().value()));
            CHECK_EQ(std::get<Error>(output), Error{LazyString{}});
          }},
     {.name = L"NonEmptyCommandNoMatch",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
            gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
            gc::Pool pool({});
            gc::Root<vm::Environment> environment = vm::Environment::New(pool);
            ValueOrError<ParsedCommand> output =
                Parse(pool, LazyString{L"foo bar"}, environment.ptr().value(),
                      LazyString(),
                      std::unordered_set<vm::Type>({vm::types::String{}}),
                      SearchNamespaces(buffer.ptr().value()));
            Error error = std::get<Error>(output);
            LOG(INFO) << "Error: " << error;
            CHECK_GT(error.read().size(),
                     ColumnNumberDelta{sizeof("Unknown ")});
            CHECK(error.read().Substring(
                      ColumnNumber{},
                      ColumnNumberDelta{sizeof("Unknown ") - 1}) ==
                  LazyString{L"Unknown "});
          }},
     {.name = L"CommandMatch", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
        gc::Pool pool({});
        gc::Root<vm::Environment> environment = vm::Environment::New(pool);
        environment.ptr()->Define(
            Identifier{LazyString{L"foo"}},
            vm::Value::NewString(pool, LazyString{L"bar"}));
        ValueOrError<ParsedCommand> output = Parse(
            pool, LazyString{L"foo"}, environment.ptr().value(), LazyString(),
            std::unordered_set<vm::Type>({vm::types::String{}}),
            SearchNamespaces(buffer.ptr().value()));
        CHECK(!std::holds_alternative<Error>(output));
      }}});
}  // namespace

futures::ValueOrError<gc::Root<vm::Value>> Execute(
    OpenBuffer& buffer, ParsedCommand parsed_command) {
  if (parsed_command.expression->Types().empty()) {
    // TODO: Show the error.
    return futures::Past(
        Error{LazyString{L"Unable to compile (type mismatch)."}});
  }
  return buffer.EvaluateExpression(std::move(parsed_command.expression),
                                   buffer.environment().ToRoot());
}

futures::Value<EmptyValue> RunCppCommandShellHandler(EditorState& editor_state,
                                                     SingleLine command) {
  return RunCppCommandShell(command, editor_state)
      .Transform([](auto) { return Success(); })
      .ConsumeErrors([](auto) { return futures::Past(EmptyValue()); });
}

futures::Value<ColorizePromptOptions> CppColorizeOptionsProvider(
    EditorState& editor, SingleLine line,
    NonNull<std::shared_ptr<ProgressChannel>> progress_channel,
    DeleteNotification::Value) {
  std::optional<gc::Root<OpenBuffer>> buffer = editor.current_buffer();
  if (!buffer.has_value()) return futures::Past(ColorizePromptOptions());
  LineModifierSet modifiers;
  return std::visit(
      overload{
          [&](std::pair<language::NonNull<std::unique_ptr<vm::Expression>>,
                        language::gc::Root<vm::Environment>>
                  compilation_result) {
            modifiers.insert(LineModifier::kCyan);
            progress_channel->Push(
                {.values = {
                     {VersionPropertyKey{LazyString{L"type"}},
                      vm::TypesToString(compilation_result.first->Types())}}});
            ColorizePromptOptions output{
                .tokens = {{.token = {.value = LazyString{},
                                      .begin = ColumnNumber(0),
                                      .end = ColumnNumber(0) + line.size()},
                            .modifiers = modifiers}}};
            if (compilation_result.first->Types() ==
                std::vector<vm::Type>({vm::types::Void{}}))
              return futures::Past(output);

            if (compilation_result.first->purity().writes_external_outputs)
              return futures::Past(output);
            return buffer->ptr()
                ->EvaluateExpression(std::move(compilation_result.first),
                                     compilation_result.second)
                .Transform([progress_channel](gc::Root<vm::Value> value) {
                  std::ostringstream oss;
                  oss << value.ptr().value();
                  progress_channel->Push(
                      {.values = {{VersionPropertyKey{LazyString{L"value"}},
                                   LazyString{FromByteString(oss.str())}}}});
                  return futures::Past(Success());
                })
                .ConsumeErrors([progress_channel](Error error) {
                  progress_channel->Push(
                      {.values = {{VersionPropertyKey{LazyString{L"runtime"}},
                                   error.read()}}});
                  return futures::Past(EmptyValue());
                })
                .Transform([output](EmptyValue) { return output; });
          },
          [&](Error error) {
            progress_channel->Push(
                {.values = {{VersionPropertyKey{LazyString{L"error"}},
                             error.read()}}});
            return futures::Past(ColorizePromptOptions());
          }},
      buffer->ptr()->CompileString(line.read()));
}

futures::Value<ColorizePromptOptions> ColorizeOptionsProvider(
    EditorState& editor, const SearchNamespaces& search_namespaces,
    Predictor predictor, SingleLine line,
    NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
    DeleteNotification::Value abort_value) {
  VLOG(7) << "ColorizeOptionsProvider: " << line;
  NonNull<std::shared_ptr<ColorizePromptOptions>> output;
  std::optional<gc::Root<OpenBuffer>> buffer = editor.current_buffer();
  vm::Environment& environment =
      (buffer.has_value() ? buffer->ptr()->environment()
                          : editor.environment().ptr())
          .value();

  std::visit(
      overload{IgnoreErrors{},
               [&](ParsedCommand) {
                 output->tokens.push_back(
                     {.token = {.value = LazyString{},
                                .begin = ColumnNumber(0),
                                .end = ColumnNumber() + line.size()},
                      .modifiers = {LineModifier::kCyan}});
               }},
      // TODO(trivial, 2024-09-13): Remove read().
      Parse(editor.gc_pool(), line.read(), environment, search_namespaces));

  using BufferMapper = vm::VMTypeMapper<gc::Ptr<editor::OpenBuffer>>;
  return Predict(predictor,
                 PredictorInput{.editor = editor,
                                // TODO(trivial, 2024-09-13): Remove read().
                                .input = line.read(),
                                .input_column = ColumnNumber() + line.size(),
                                .source_buffers = editor.active_buffers(),
                                .progress_channel = std::move(progress_channel),
                                .abort_value = std::move(abort_value)})
      .Transform([output](std::optional<PredictResults> results) {
        if (results.has_value())
          output->context = ColorizePromptOptions::ContextBuffer{
              .buffer = results->predictions_buffer};
        return futures::Past(EmptyValue());
      })
      .Transform([&editor, search_namespaces, line, output, buffer,
                  &environment](EmptyValue) -> futures::Value<EmptyValue> {
        return std::visit(
            overload{
                [&](Error error) {
                  VLOG(4) << "Parse preview error: " << error;
                  return futures::Past(EmptyValue());
                },
                [&](ParsedCommand command) -> futures::Value<EmptyValue> {
                  VLOG(4) << "Successfully parsed Preview command: "
                          << command.tokens[0].value
                          << ", buffer: " << buffer->ptr()->name();
                  return Execute(buffer->ptr().value(), std::move(command))
                      .Transform(
                          [buffer, output](gc::Root<vm::Value> value) mutable {
                            VLOG(3) << "Successfully executed Preview command: "
                                    << value.ptr().value();
                            if (value.ptr()->type ==
                                vm::GetVMType<
                                    gc::Ptr<editor::OpenBuffer>>::vmtype()) {
                              output->context =
                                  ColorizePromptOptions::ContextBuffer{
                                      .buffer =
                                          BufferMapper::get(value.ptr().value())
                                              .ToRoot()};
                            }
                            return futures::Past(Success());
                          })
                      .ConsumeErrors(
                          [](Error) { return futures::Past(EmptyValue()); });
                }},
            buffer.has_value()
                // TODO(trivial, 2024-09-13): Remove read().
                ? Parse(editor.gc_pool(), line.read(), environment,
                        LazyString{L"Preview"},
                        {vm::GetVMType<gc::Ptr<editor::OpenBuffer>>::vmtype()},
                        search_namespaces)
                : ValueOrError<ParsedCommand>(
                      Error{LazyString{L"Buffer has no value"}}));
      })
      .Transform([output](EmptyValue) {
        return futures::Past(std::move(output.value()));
      });
}

std::vector<LazyString> GetCppTokens(
    std::optional<gc::Root<OpenBuffer>> buffer) {
  std::vector<LazyString> output;
  std::set<Identifier> output_set;  // Avoid duplicates.
  if (buffer.has_value())
    buffer->ptr()->environment()->ForEach(
        [&output, &output_set](Identifier name,
                               const gc::Ptr<vm::Value>& value) {
          // TODO(easy, 2023-09-16): Would be good to filter more stringently.
          VLOG(10) << "Checking symbol: " << name;
          if (value->IsFunction() && output_set.insert(name).second)
            output.push_back(LowerCase(name.read()));
        });
  VLOG(4) << "Found tokens: " << output.size();
  return output;
}
}  // namespace

futures::ValueOrError<gc::Root<vm::Value>> RunCppCommandShell(
    const SingleLine& command, EditorState& editor_state) {
  using futures::Past;
  auto buffer = editor_state.current_buffer();
  if (!buffer.has_value()) {
    return Past(ValueOrError<gc::Root<vm::Value>>(
        Error{LazyString{L"No active buffer."}}));
  }
  buffer->ptr()->ResetMode();

  SearchNamespaces search_namespaces(buffer->ptr().value());
  return std::visit(
      overload{[&](Error error) {
                 if (!error.read().empty()) buffer->ptr()->status().Set(error);
                 return Past(ValueOrError<gc::Root<vm::Value>>(
                     Error{LazyString{L"Unable to parse command"}}));
               },
               [&](ParsedCommand parsed_command) {
                 return futures::OnError(
                     Execute(buffer->ptr().value(), std::move(parsed_command)),
                     [buffer](Error error) {
                       buffer->ptr()->status().Set(error);
                       return futures::Past(error);
                     });
               }},
      Parse(editor_state.gc_pool(), command.read(),
            buffer->ptr()->environment().value(), search_namespaces));
}

gc::Root<Command> NewRunCppCommand(EditorState& editor_state,
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
        LazyString prompt;
        std::function<futures::Value<EmptyValue>(SingleLine input)> handler;
        PromptOptions::ColorizeFunction colorize_options_provider;
        Predictor predictor = EmptyPredictor;
        switch (mode) {
          case CppCommandMode::kLiteral:
            handler = std::bind_front(RunCppCommandLiteralHandler,
                                      std::ref(editor_state));
            prompt = LazyString{L"cpp"};
            colorize_options_provider = std::bind_front(
                CppColorizeOptionsProvider, std::ref(editor_state));
            break;
          case CppCommandMode::kShell:
            handler = std::bind_front(RunCppCommandShellHandler,
                                      std::ref(editor_state));
            prompt = LazyString{L":"};
            auto buffer = editor_state.current_buffer();
            CHECK(buffer.has_value());
            // TODO(easy, 2023-09-16): Make it possible to disable the use of a
            // separator, so that we don't have to stupidly pass some character
            // that doesn't occur.
            predictor = PrecomputedPredictor(
                GetCppTokens(editor_state.current_buffer()), L' ');
            colorize_options_provider = std::bind_front(
                ColorizeOptionsProvider, std::ref(editor_state),
                SearchNamespaces(buffer->ptr().value()), predictor);
            break;
        }
        return PromptOptions{
            .editor_state = editor_state,
            .prompt = prompt + LazyString{L" "},
            .history_file = prompt == LazyString{L":"}
                                ? HistoryFile{LazyString{L"colon"}}
                                : HistoryFile{prompt},
            .colorize_options_provider = std::move(colorize_options_provider),
            .handler = std::move(handler),
            .cancel_handler = []() { /* Nothing. */ },
            .predictor = predictor,
            .status = PromptOptions::Status::kBuffer};
      });
}
}  // namespace afc::editor
