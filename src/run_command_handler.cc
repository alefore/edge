#include "src/run_command_handler.h"

#include <glog/logging.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/file_predictor.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/time.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/run_command.h"
#include "src/token_predictor.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/escape.h"
#include "src/vm/function_call.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace staging = afc::language::staging;

using afc::futures::DeleteNotification;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::GetElapsedSecondsSince;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::ProcessId;
using afc::infrastructure::screen::Color;using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::HasValue;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::vm::EscapedString;

namespace afc {
namespace editor {
namespace {
void RunCommand(const CommandBufferName& name,
                std::map<std::wstring, LazyString> environment,
                EditorState& editor_state, std::optional<Path> children_path,
                LazyString input) {
  auto buffer = editor_state.current_buffer();
  if (input.size().IsZero()) {
    if (buffer.has_value()) {
      buffer->ptr()->ResetMode();
      buffer->ptr()->status().Reset();
    }
    editor_state.status().Reset();
    return;
  }

  RunCommand(editor_state,
             RunCommandOptions{
                 .command = input,
                 .name = name,
                 .environment = std::move(environment),
                 .insertion_type =
                     buffer.has_value() &&
                             buffer->ptr()->Read(
                                 buffer_variables::commands_background_mode)
                         ? BuffersList::AddBufferType::Ignore
                         : BuffersList::AddBufferType::Visit,
                 .children_path = children_path,
             });
}

// Input must already be unescaped (e.g., contain `\n` rather than `\\n`).
futures::Value<EmptyValue> RunCommandHandler(EditorState& editor_state,
                                             size_t i, size_t n,
                                             std::optional<Path> children_path,
                                             LazyString input) {
  std::map<std::wstring, LazyString> environment = {
      {L"EDGE_RUN", LazyString{std::to_wstring(i)}},
      {L"EDGE_RUNS", LazyString{std::to_wstring(n)}}};
  LazyString name =
      (children_path.has_value() ? children_path->read() : LazyString{}) +
      LazyString{L"$"};
  if (n > 1) {
    name += Concatenate(environment | std::views::transform([](auto it) {
                          return LazyString{L" "} + LazyString{it.first} +
                                 LazyString{L"="} + it.second;
                        }));
  }
  auto buffer = editor_state.current_buffer();
  if (buffer.has_value()) {
    environment[L"EDGE_SOURCE_BUFFER_PATH"] =
        buffer->ptr()->Read(buffer_variables::path);
  }
  name += LazyString{L" "} +
          EscapedString::FromString(input).EscapedRepresentation().read();
  RunCommand(CommandBufferName{name}, environment, editor_state, children_path,
             input);
  return EmptyValue{};
}

ValueOrError<Path> GetChildrenPath(EditorState& editor_state) {
  if (auto buffer = editor_state.current_buffer(); buffer.has_value()) {
    return AugmentError(
        LazyString{L"Getting children path of buffer"},
        Path::New(buffer->ptr()->Read(buffer_variables::children_path)));
  }
  return Error{LazyString{L"Editor doesn't have a current buffer."}};
}

class ForkEditorCommand : public Command {
 private:
  // Holds information about the current state of the prompt.
  struct PromptState {
    const gc::Root<OpenBuffer> original_buffer;
    std::optional<LazyString> base_command;
    std::optional<gc::Root<afc::vm::Value>> context_command_callback;
  };

 public:
  ForkEditorCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{
        L"Prompts for a command and creates a new buffer running it."};
  }
  CommandCategory Category() const override {
    return CommandCategory::kBuffers();
  }

  void ProcessInput(ExtendedChar) override {
    if (editor_state_.structure() == Structure::Char) {
      std::optional<gc::Root<OpenBuffer>> original_buffer =
          editor_state_.current_buffer();
      // TODO(easy, 2022-05-16): Why is this safe?
      CHECK(original_buffer.has_value());
      static const vm::Namespace kEmptyNamespace;
      vm::Identifier callback_identifier =
          IDENTIFIER_CONSTANT(L"GetShellPromptContextProgram");
      std::optional<vm::Environment::LookupResult> callback =
          original_buffer->ptr()->environment()->Lookup(
              kEmptyNamespace, callback_identifier,
              vm::types::Function{.output = vm::Type{vm::types::String{}},
                                  .inputs = {vm::types::String{}}});
      CHECK(callback.has_value())
          << callback_identifier << ": Required symbol is not defined.";
      CHECK(std::holds_alternative<gc::Root<vm::Value>>(callback->value))
          << callback_identifier
          << ": Required symbol is defined but not initialized.";
      NonNull<std::shared_ptr<PromptState>> prompt_state =
          MakeNonNullShared<PromptState>(PromptState{
              .original_buffer = *original_buffer,
              .base_command = std::nullopt,
              .context_command_callback =
                  std::get<gc::Root<vm::Value>>(std::move(callback)->value)});
      ValueOrError<Path> children_path = GetChildrenPath(editor_state_);
      LineBuilder prompt;
      VisitValue(children_path, [&prompt](Path path) {
        prompt.AppendString(LineSequence::BreakLines(path.read()).FoldLines());
      });
      prompt.AppendString(SINGLE_LINE_CONSTANT(L"$ "), Style{StandardColor::Green});
      Prompt(PromptOptions{
          .editor_state = editor_state_,
          .prompt = std::move(prompt).Build(),
          .history_file = HistoryFileCommands(),
          .colorize_options_provider =
              prompt_state->context_command_callback.has_value()
                  ? ([prompt_state](const SingleLine& line,
                                    NonNull<std::unique_ptr<ProgressChannel>>,
                                    DeleteNotification::Value) {
                      return PromptChange(prompt_state.value(), line);
                    })
                  : PromptOptions::ColorizeFunction(nullptr),
          .handler =
              [&editor = editor_state_, children_path](SingleLine input) {
                return RunCommandHandler(
                    editor, 0, 1, OptionalFrom(children_path), input.read());
              },
          .predictor =
              TokenPredictor(GetFilePredictor(FilePredictorOptions{}))});
    } else if (editor_state_.structure() == Structure::Line) {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      if (!buffer.has_value()) {
        return;
      }
      VisitPointer(
          buffer->ptr()->OptionalCurrentLine(),
          [&](const Line& current_line) {
            VisitValue(
                editor_state_.status().LogErrors(
                    EscapedString::Parse(current_line.contents())),
                [&](EscapedString line) {
                  std::optional<Path> children_path =
                      OptionalFrom(GetChildrenPath(editor_state_));
                  for (size_t i = 0;
                       i < editor_state_.repetitions().value_or(1); ++i) {
                    RunCommandHandler(editor_state_, i,
                                      editor_state_.repetitions().value_or(1),
                                      children_path, line.OriginalString());
                  }
                });
          },
          [] {});
    } else {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      (buffer.has_value() ? buffer->ptr()->status() : editor_state_.status())
          .InsertError(
              Error{LazyString{L"Oops, that structure is not handled."}});
    }
    editor_state_.ResetStructure();
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  static futures::Value<ColorizePromptOptions> PromptChange(
      PromptState& prompt_state, const SingleLine& line) {
    CHECK(prompt_state.context_command_callback.has_value());
    EditorState& editor = prompt_state.original_buffer.ptr()->editor();
    language::gc::Pool& pool = editor.gc_pool();
    CHECK(editor.status().GetType() == Status::Type::Prompt);
    gc::Root<vm::Expression> context_command_expression = vm::NewFunctionCall(
        vm::NewConstantExpression(prompt_state.context_command_callback->ptr())
            .ptr(),
        {vm::NewConstantExpression(
             vm::Value::NewString(pool, ToLazyString(line)).ptr())
             .ptr()});
    if (context_command_expression->Types().empty()) {
      prompt_state.base_command = std::nullopt;
      prompt_state.original_buffer.ptr()->status().InsertError(
          Error{LazyString{L"Unable to compile (type mismatch)."}});
      return ColorizePromptOptions{.context =
                                       ColorizePromptOptions::ContextClear()};
    }
    return prompt_state.original_buffer.ptr()
        ->EvaluateExpression(context_command_expression.ptr(),
                             prompt_state.original_buffer.ptr()->environment())
        .Transform([&prompt_state,
                    &editor](gc::Root<vm::Value> context_command_output)
                       -> ValueOrError<ColorizePromptOptions> {
          LazyString base_command = context_command_output.ptr()->get_string();
          if (prompt_state.base_command == base_command)
            return ColorizePromptOptions{};
          if (base_command.empty()) {
            prompt_state.base_command = std::nullopt;
            return ColorizePromptOptions{
                .context = ColorizePromptOptions::ContextClear()};
          }

          prompt_state.base_command = base_command;
          RunCommandOptions options;
          options.command = base_command;
          options.name = BufferName{LazyString{L"- preview: "} + base_command};
          options.insertion_type = BuffersList::AddBufferType::Ignore;
          gc::Root<OpenBuffer> help_buffer_root = RunCommand(editor, options);
          OpenBuffer& help_buffer = help_buffer_root.ptr().value();
          help_buffer.Set(buffer_variables::follow_end_of_file, false);
          help_buffer.Set(buffer_variables::show_in_buffers_list, false);
          help_buffer.Set(buffer_variables::allow_dirty_delete, true);
          help_buffer.set_position({});
          return ColorizePromptOptions{.context =
                                           ColorizePromptOptions::ContextBuffer{
                                               .buffer = help_buffer_root}};
        })
        .ConsumeErrors([](Error) { return ColorizePromptOptions{}; });
  }

  EditorState& editor_state_;
};

}  // namespace
}  // namespace editor
namespace vm {
template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<editor::RunCommandOptions>>>::object_type_name =
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"RunCommandOptions")}};
}  // namespace vm
namespace editor {
/* static */
void RunCommandOptions::Register(gc::Pool& pool, vm::Environment& environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMTypeMapper;
  gc::Root<ObjectType> fork_command_options = ObjectType::New(
      pool, VMTypeMapper<
                NonNull<std::shared_ptr<RunCommandOptions>>>::object_type_name);

  environment.Define(vm::Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"RunCommandOptions"}}}},
                     NewCallback(pool, vm::kPurityTypePure,
                                 MakeNonNullShared<RunCommandOptions>));

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_command"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<RunCommandOptions>> options,
                     LazyString value) { options->command = std::move(value); })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_name"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<RunCommandOptions>> options,
                     LazyString value) {
                    options->name = CommandBufferName{std::move(value)};
                  })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_insertion_type"}}}},
      NewCallback(
          pool, vm::kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<RunCommandOptions>> options,
             std::wstring value) {
            if (value == L"visit") {
              options->insertion_type = BuffersList::AddBufferType::Visit;
            } else if (value == L"only_list") {
              options->insertion_type = BuffersList::AddBufferType::OnlyList;
            } else if (value == L"ignore") {
              options->insertion_type = BuffersList::AddBufferType::Ignore;
            }
          })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_children_path"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<RunCommandOptions>> options,
                     LazyString value) {
                    options->children_path =
                        OptionalFrom(Path::New(std::move(value)));
                  })
          .ptr());

  environment.DefineType(fork_command_options.ptr());
}

gc::Root<Command> NewRunCommandCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<ForkEditorCommand>(editor_state));
}

futures::Value<EmptyValue> RunCommandHandler(
    EditorState& editor_state, std::map<std::wstring, LazyString> environment,
    LazyString input, SingleLine name_suffix) {
  RunCommand(CommandBufferName{input + name_suffix}, environment, editor_state,
             OptionalFrom(GetChildrenPath(editor_state)), input);
  return EmptyValue{};
}

futures::Value<EmptyValue> RunMultipleCommandsHandler(EditorState& editor_state,
                                                      SingleLine input) {
  return editor_state
      .ForEachActiveBuffer([&editor_state, input](OpenBuffer& buffer) {
        std::ranges::for_each(
            buffer.contents().snapshot(),
            [&editor_state, input](const staging::Value<Line>& arg) {
              RunCommandHandler(editor_state,
                                std::map<std::wstring, LazyString>{
                                    {L"ARG", arg->contents().read()}},
                                input.read(),
                                SingleLine{LazyString{L" "}} + arg->contents());
            });
        return EmptyValue{};
      })
      .Transform([&editor_state](EmptyValue) {
        editor_state.status().Reset();
        return EmptyValue();
      });
}

}  // namespace editor
}  // namespace afc
