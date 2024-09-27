#include "src/help_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/parsers/markdown.h"
#include "src/terminal.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
LineBuilder DescribeSequence(const std::vector<ExtendedChar>& input) {
  LineBuilder output;
  for (const ExtendedChar& c : input)
    std::visit(
        overload{
            [&](wchar_t regular_c) {
              switch (regular_c) {
                case '\t':
                  output.AppendString(SINGLE_LINE_CONSTANT(L"Tab"),
                                      std::nullopt);
                  break;
                case '\n':
                  output.AppendString(SINGLE_LINE_CONSTANT(L"↩"), std::nullopt);
                  break;
                default:
                  output.AppendString(
                      SingleLine{LazyString{ColumnNumberDelta{1}, regular_c}},
                      std::nullopt);
              }
            },
            [&](ControlChar control) {
              switch (control) {
                case ControlChar::kEscape:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"Esc"),
                                      std::nullopt);
                  break;
                case ControlChar::kDownArrow:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"↓"), std::nullopt);
                  break;
                case ControlChar::kUpArrow:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"↑"), std::nullopt);
                  break;
                case ControlChar::kLeftArrow:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"←"), std::nullopt);
                  break;
                case ControlChar::kRightArrow:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"→"), std::nullopt);
                  break;
                case ControlChar::kBackspace:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"← Backspace"),
                                      std::nullopt);
                  break;
                case ControlChar::kPageDown:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"PgDn"),
                                      std::nullopt);
                  break;
                case ControlChar::kPageUp:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"PgUp"),
                                      std::nullopt);
                  break;
                case ControlChar::kHome:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"Home"),
                                      std::nullopt);
                  break;
                case ControlChar::kEnd:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"End"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlA:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^a"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlD:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^d"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlE:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^e"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlK:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^k"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlL:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^l"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlU:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^u"),
                                      std::nullopt);
                  break;
                case ControlChar::kCtrlV:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"^v"),
                                      std::nullopt);
                  break;
                case ControlChar::kDelete:
                  output.AppendString(SINGLE_LINE_CONSTANT(L"Delete"),
                                      std::nullopt);
                  break;
              }
            }},
        c);
  return output;
}

LineBuilder DescribeSequenceWithQuotes(
    const std::vector<infrastructure::ExtendedChar>& input) {
  LineBuilder output;
  output.AppendString(SINGLE_LINE_CONSTANT(L"`"),
                      LineModifierSet{LineModifier::kDim});
  output.Append(DescribeSequence(input));
  output.AppendString(SINGLE_LINE_CONSTANT(L"`"),
                      LineModifierSet{LineModifier::kDim});
  return output;
}

namespace {
class HelpCommand : public Command {
 public:
  HelpCommand(EditorState& editor_state, const MapModeCommands& commands,
              const std::wstring& mode_description)
      : editor_state_(editor_state),
        commands_(commands),
        mode_description_(mode_description) {}

  LazyString Description() const override {
    return LazyString{L"Shows documentation."};
  }
  CommandCategory Category() const override {
    return CommandCategory::kEditor();
  }

  void ProcessInput(ExtendedChar) override {
    VisitOptional(
        [&](gc::Root<OpenBuffer> original_buffer) {
          const BufferName name(LazyString{L"- help: "} +
                                LazyString{mode_description_});
          gc::Root<OpenBuffer> buffer_root =
              OpenBuffer::New(OpenBuffer::Options{
                  .editor = editor_state_,
                  .name = name,
                  .generate_contents = std::bind_front(
                      GenerateContents, std::ref(commands_), original_buffer)});
          buffer_root.ptr()->Reload();
          editor_state_.AddBuffer(std::move(buffer_root),
                                  BuffersList::AddBufferType::kVisit);
          editor_state_.ResetRepetitions();
        },
        [] {}, editor_state_.current_buffer());
  }

  static futures::Value<language::PossibleError> GenerateContents(
      const MapModeCommands& commands, gc::Root<OpenBuffer> input,
      OpenBuffer& output) {
    output.Set(buffer_variables::tree_parser,
               language::lazy_string::ToLazyString(ParserId::Markdown()));
    output.Set(buffer_variables::wrap_from_content, true);
    output.Set(buffer_variables::allow_dirty_delete, true);
    output.InsertInPosition(GenerateLines(commands, input.ptr().value()),
                            LineColumn(), {});
    output.set_current_position_line(LineNumber(0));
    return futures::Past(EmptyValue());
  }

  static LineSequence GenerateLines(const MapModeCommands& commands,
                                    const OpenBuffer& buffer) {
    LOG(INFO) << "Generating help contents.";
    MutableLineSequence output;
    output.AppendToLine(LineNumber(),
                        Line{SingleLine{LazyString{L"# Edge - Help"}}});
    output.push_back(L"");

    ShowCommands(commands, output);
    ShowEnvironment(buffer, output);

    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"## Buffer Variables"),
                 output);
    output.push_back(
        L"The following are all the buffer variables defined for your buffer.");
    output.push_back(L"");

    DescribeVariables(
        SingleLine{LazyString{L"bool"}}, buffer, output,
        buffer_variables::BoolStruct(),
        [](const bool& value) {
          return value ? SingleLine{LazyString{L"true"}}
                       : SingleLine{LazyString{L"false"}};
        },
        &OpenBuffer::Read);
    DescribeVariables(
        SingleLine{LazyString{L"string"}}, buffer, output,
        buffer_variables::StringStruct(),
        [](const LazyString& value) {
          return SingleLine{LazyString{L"`"}} +
                 vm::EscapedString(value).EscapedRepresentation() +
                 SingleLine{LazyString{L"`"}};
        },
        &OpenBuffer::Read);
    DescribeVariables(
        SingleLine{LazyString{L"int"}}, buffer, output,
        buffer_variables::IntStruct(),
        [](const int& value) {
          return SingleLine{LazyString{std::to_wstring(value)}};
        },
        &OpenBuffer::Read);

    CommandLineVariables(output);
    return output.snapshot();
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  static void StartSection(NonEmptySingleLine section,
                           MutableLineSequence& buffer) {
    VLOG(2) << "Section: " << section;
    buffer.push_back(Line{std::move(section).read()});
    buffer.push_back(Line{});
  }

  static void ShowCommands(const MapModeCommands& commands,
                           MutableLineSequence& output) {
    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"## Commands"), output);

    output.push_back(
        L"The following is a list of all commands available in "
        L"your buffer, grouped by category.");
    output.push_back(L"");

    for (const auto& category : commands.Coallesce()) {
      StartSection(
          NON_EMPTY_SINGLE_LINE_CONSTANT(L"### ") + category.first.read(),
          output);
      for (const auto& [input, command] : category.second) {
        LineBuilder line;
        line.AppendString(SINGLE_LINE_CONSTANT(L"* "), std::nullopt);
        line.Append(DescribeSequenceWithQuotes(input));
        line.AppendString(SINGLE_LINE_CONSTANT(L" - ") +
                              // TODO(easy, 2024-09-19): Avoid having to wrap in
                              // SingleLine here:
                              SingleLine{command->Description()},
                          std::nullopt);
        output.push_back(std::move(line).Build());
      }
      output.push_back(L"");
    }
  }

  // This is public for testability.
  static void ShowEnvironment(const OpenBuffer& original_buffer,
                              MutableLineSequence& output) {
    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"## Environment"), output);

    const gc::Ptr<vm::Environment> environment = original_buffer.environment();

    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"### Types & methods"),
                 output);

    output.push_back(
        L"This section contains a list of all types available to Edge "
        L"extensions running in your buffer. For each, a list of all their "
        L"available methods is given.");
    output.push_back(L"");

    environment->ForEachType(
        [&](const vm::types::ObjectName& name, vm::ObjectType& type) {
          StartSection(
              NON_EMPTY_SINGLE_LINE_CONSTANT(L"#### ") + name.read().read(),
              output);
          type.ForEachField([&](const vm::Identifier& field_name,
                                vm::Value& value) {
            std::stringstream value_stream;
            value_stream << value;
            const static ColumnNumberDelta kPaddingSize{40};
            SingleLine padding{
                LazyString{field_name.read().size() > kPaddingSize
                               ? ColumnNumberDelta{}
                               : kPaddingSize - field_name.read().size(),
                           L' '}};
            output.push_back(LineBuilder{
                (SingleLine{LazyString{L"* `"}} + field_name.read() +
                 SingleLine{LazyString{L"`"}} + std::move(padding) +
                 SingleLine{LazyString{L"`"}} +
                 SingleLine{LazyString{FromByteString(value_stream.str())}} +
                 SingleLine{LazyString{L"`"}})
                    .read()}
                                 .Build());
          });
          output.push_back(L"");
        });
    output.push_back(L"");

    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"### Variables"), output);

    output.push_back(
        L"The following are all variables defined in the environment "
        L"associated with your buffer, and thus available to extensions.");
    output.push_back(L"");

    environment->ForEach(
        [&output](const vm::Identifier& name, const gc::Ptr<vm::Value>& value) {
          std::stringstream value_stream;
          value_stream << value.value();
          const static ColumnNumberDelta kPaddingSize{40};
          SingleLine padding{LazyString{name.read().size() > kPaddingSize
                                            ? ColumnNumberDelta{}
                                            : kPaddingSize - name.read().size(),
                                        L' '}};
          output.push_back(LineBuilder{
              (SingleLine{LazyString{L"* `"}} + name.read() +
               SingleLine{LazyString{L"`"}} + std::move(padding) +
               SingleLine{LazyString{L"`"}} +
               SingleLine{LazyString{FromByteString(value_stream.str())}} +
               SingleLine{LazyString{L"`"}})
                  .read()}
                               .Build());
        });
    output.push_back(L"");
  }

  // TODO(trivial, 2024-08-27): Once OpenBuffer::Read returns a LazyString,
  // get rid of parameter reader.
  template <typename T, typename C>
  static void DescribeVariables(
      SingleLine type_name, const OpenBuffer& source,
      MutableLineSequence& output, EdgeStruct<T>* variables, C print,
      const T& (OpenBuffer::*reader)(const EdgeVariable<T>*) const) {
    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"### ") + type_name, output);
    for (const auto& variable : variables->variables()) {
      // TODO(trivial, 2024-09-17): Stop wrapping variable.second->name in
      // SingleLine here.
      output.push_back(
          LineBuilder{SingleLine{LazyString{L"#### "}} +
                      SingleLine{LazyString{variable.second->name()}}}
              .Build());
      output.push_back(L"");
      output.push_back(variable.second->description());
      output.push_back(L"");
      output.push_back(
          LineBuilder{SingleLine{LazyString{L"* Value: "}} +
                      print((source.*reader)(&variable.second.value()))}
              .Build());
      output.push_back(LineBuilder{SingleLine{LazyString{L"* Default: "}} +
                                   print(variable.second->default_value())}
                           .Build());

      if (!variable.second->key().empty()) {
        output.push_back(L"* Related commands: `v" + variable.second->key() +
                         L"`");
      }
      output.push_back(L"");
    }
    output.push_back(L"");
  }

  static void CommandLineVariables(MutableLineSequence& output) {
    StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"## Command line arguments"),
                 output);
    using command_line_arguments::Handler;
    for (const Handler<CommandLineValues>& h : CommandLineArgs()) {
      StartSection(NON_EMPTY_SINGLE_LINE_CONSTANT(L"### ") +
                       h.aliases()[0].GetSingleLine(),
                   output);
      switch (h.argument_type()) {
        case Handler<CommandLineValues>::VariableType::kRequired:
          output.push_back(L"Required argument: " + h.argument() + L": " +
                           h.argument_description());
          output.push_back(L"");
          break;

        case Handler<CommandLineValues>::VariableType::kOptional:
          output.push_back(L"Optional argument: " + h.argument() + L": " +
                           h.argument_description());
          output.push_back(L"");
          break;

        case Handler<CommandLineValues>::VariableType::kNone:
          break;
      }
      output.append_back(LineSequence::BreakLines(h.help()));
      output.push_back(L"");
    }
  }

  EditorState& editor_state_;
  const MapModeCommands& commands_;
  const std::wstring mode_description_;
};

const bool buffer_registration = tests::Register(
    L"HelpCommand::GenerateContents",
    {
        {.name = L"GenerateContents",
         .callback =
             [] {
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               auto buffer = NewBufferForTests(editor.value());
               gc::Root<MapModeCommands> commands =
                   MapModeCommands::New(buffer.ptr()->editor());
               HelpCommand::GenerateLines(commands.ptr().value(),
                                          buffer.ptr().value());
             }},
    });
}  // namespace

gc::Root<Command> NewHelpCommand(EditorState& editor_state,
                                 const MapModeCommands& commands,
                                 const std::wstring& mode_description) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<HelpCommand>(editor_state, commands, mode_description));
}

}  // namespace afc::editor
