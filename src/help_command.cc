#include "src/help_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/terminal.h"
#include "src/tests/tests.h"

namespace afc::editor {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineSequence;
using language::text::MutableLineSequence;

namespace gc = language::gc;

Line DescribeSequence(const std::wstring& input) {
  LineBuilder output;
  for (auto& c : input) {
    switch (c) {
      case '\t':
        output.AppendString(L"Tab", std::nullopt);
        break;
      case '\n':
        output.AppendString(std::wstring(1, L'↩'), std::nullopt);
        break;
      case Terminal::ESCAPE:
        output.AppendString(L"Esc", std::nullopt);
        break;
      case Terminal::DOWN_ARROW:
        output.AppendString(L"↓", std::nullopt);
        break;
      case Terminal::UP_ARROW:
        output.AppendString(L"↑", std::nullopt);
        break;
      case Terminal::LEFT_ARROW:
        output.AppendString(L"←", std::nullopt);
        break;
      case Terminal::RIGHT_ARROW:
        output.AppendString(L"→", std::nullopt);
        break;
      case Terminal::BACKSPACE:
        output.AppendString(L"← Backspace", std::nullopt);
        break;
      case Terminal::PAGE_DOWN:
        output.AppendString(L"PgDn", std::nullopt);
        break;
      case Terminal::PAGE_UP:
        output.AppendString(L"PgUp", std::nullopt);
        break;
      case Terminal::CTRL_A:
        output.AppendString(L"^a", std::nullopt);
        break;
      case Terminal::CTRL_D:
        output.AppendString(L"^d", std::nullopt);
        break;
      case Terminal::CTRL_E:
        output.AppendString(L"^e", std::nullopt);
        break;
      case Terminal::CTRL_K:
        output.AppendString(L"^k", std::nullopt);
        break;
      case Terminal::CTRL_L:
        output.AppendString(L"^l", std::nullopt);
        break;
      case Terminal::CTRL_U:
        output.AppendString(L"^u", std::nullopt);
        break;
      case Terminal::CTRL_V:
        output.AppendString(L"^v", std::nullopt);
        break;
      case Terminal::DELETE:
        output.AppendString(L"Delete", std::nullopt);
        break;
      default:
        output.AppendString(std::wstring(1, static_cast<wchar_t>(c)),
                            std::nullopt);
    }
  }
  return std::move(output).Build();
}

Line DescribeSequenceWithQuotes(const std::wstring& input) {
  LineBuilder output;
  output.AppendString(L"`", LineModifierSet{LineModifier::kDim});
  output.Append(LineBuilder(DescribeSequence(input)));
  output.AppendString(L"`", LineModifierSet{LineModifier::kDim});
  return std::move(output).Build();
}

namespace {
class HelpCommand : public Command {
 public:
  HelpCommand(EditorState& editor_state, const MapModeCommands& commands,
              const std::wstring& mode_description)
      : editor_state_(editor_state),
        commands_(commands),
        mode_description_(mode_description) {}

  std::wstring Description() const override { return L"Shows documentation."; }
  std::wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t) override {
    auto original_buffer = editor_state_.current_buffer();
    const BufferName name(L"- help: " + mode_description_);

    auto buffer_root = OpenBuffer::New({.editor = editor_state_, .name = name});
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.Set(buffer_variables::tree_parser, L"md");
    buffer.Set(buffer_variables::wrap_from_content, true);
    buffer.Set(buffer_variables::allow_dirty_delete, true);

    // TODO(easy, 2022-05-15): Why is the following check safe?
    CHECK(original_buffer.has_value());
    buffer.InsertInPosition(
        GenerateContents(commands_, original_buffer->ptr().value()),
        LineColumn(), {});
    buffer.set_current_position_line(LineNumber(0));
    buffer.ResetMode();

    editor_state_.AddBuffer(buffer_root, BuffersList::AddBufferType::kVisit);
    editor_state_.buffers()->insert_or_assign(name, std::move(buffer_root));
    editor_state_.ResetRepetitions();
  }

  static LineSequence GenerateContents(const MapModeCommands& commands,
                                       const OpenBuffer& buffer) {
    LOG(INFO) << "Generating help contents.";
    MutableLineSequence output;
    output.AppendToLine(LineNumber(), Line(L"# Edge - Help"));
    output.push_back(L"");

    ShowCommands(commands, output);
    ShowEnvironment(buffer, output);

    StartSection(L"## Buffer Variables", output);
    output.push_back(
        L"The following are all the buffer variables defined for your buffer.");
    output.push_back(L"");

    DescribeVariables(
        L"bool", buffer, output, buffer_variables::BoolStruct(),
        [](const bool& value) { return value ? L"true" : L"false"; });
    DescribeVariables(
        L"string", buffer, output, buffer_variables::StringStruct(),
        [](const std::wstring& value) { return L"`" + value + L"`"; });
    DescribeVariables(L"int", buffer, output, buffer_variables::IntStruct(),
                      [](const int& value) { return std::to_wstring(value); });

    CommandLineVariables(output);
    return output.snapshot();
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  static void StartSection(std::wstring section, MutableLineSequence& buffer) {
    VLOG(2) << "Section: " << section;
    buffer.push_back(std::move(section));
    buffer.push_back(L"");
  }

  static void ShowCommands(const MapModeCommands& commands,
                           MutableLineSequence& output) {
    StartSection(L"## Commands", output);

    output.push_back(
        L"The following is a list of all commands available in "
        L"your buffer, grouped by category.");
    output.push_back(L"");

    for (const auto& category : commands.Coallesce()) {
      StartSection(L"### " + category.first, output);
      for (const auto& [input, command] : category.second) {
        LineBuilder line;
        line.AppendString(L"* ", std::nullopt);
        line.Append(LineBuilder(DescribeSequenceWithQuotes(input)));
        line.AppendString(L" - " + command->Description(), std::nullopt);
        output.push_back(MakeNonNullShared<Line>(std::move(line).Build()));
      }
      output.push_back(L"");
    }
  }

  // This is public for testability.
  static void ShowEnvironment(const OpenBuffer& original_buffer,
                              MutableLineSequence& output) {
    StartSection(L"## Environment", output);

    const gc::Ptr<vm::Environment> environment = original_buffer.environment();

    StartSection(L"### Types & methods", output);

    output.push_back(
        L"This section contains a list of all types available to Edge "
        L"extensions running in your buffer. For each, a list of all their "
        L"available methods is given.");
    output.push_back(L"");

    environment->ForEachType([&](const vm::types::ObjectName& name,
                                 vm::ObjectType& type) {
      StartSection(L"#### " + name.read(), output);
      type.ForEachField([&](const std::wstring& field_name, vm::Value& value) {
        std::stringstream value_stream;
        value_stream << value;
        const static int kPaddingSize = 40;
        std::wstring padding(field_name.size() > kPaddingSize
                                 ? 0
                                 : kPaddingSize - field_name.size(),
                             L' ');
        output.push_back(MakeNonNullShared<Line>(
            LineBuilder(Append(NewLazyString(L"* `"), NewLazyString(field_name),
                               NewLazyString(L"`" + std::move(padding) + L"`"),
                               NewLazyString(
                                   FromByteString(value_stream.str()) + L"`")))
                .Build()));
      });
      output.push_back(L"");
    });
    output.push_back(L"");

    StartSection(L"### Variables", output);

    output.push_back(
        L"The following are all variables defined in the environment "
        L"associated with your buffer, and thus available to extensions.");
    output.push_back(L"");

    environment->ForEach([&output](const std::wstring& name,
                                   const gc::Ptr<vm::Value>& value) {
      const static int kPaddingSize = 40;
      std::wstring padding(
          name.size() >= kPaddingSize ? 1 : kPaddingSize - name.size(), L' ');

      std::stringstream value_stream;
      value_stream << value.value();

      output.push_back(MakeNonNullShared<Line>(
          LineBuilder(
              Append(NewLazyString(L"* `"), NewLazyString(name),
                     NewLazyString(L"`" + std::move(padding) + L"`"),
                     NewLazyString(FromByteString(value_stream.str()) + L"`")))
              .Build()));
    });
    output.push_back(L"");
  }

  template <typename T, typename C>
  static void DescribeVariables(std::wstring type_name,
                                const OpenBuffer& source,
                                MutableLineSequence& output,
                                EdgeStruct<T>* variables, C print) {
    StartSection(L"### " + type_name, output);
    for (const auto& variable : variables->variables()) {
      output.push_back(MakeNonNullShared<Line>(
          LineBuilder(Append(NewLazyString(L"#### "),
                             NewLazyString(variable.second->name())))
              .Build()));
      output.push_back(L"");
      output.push_back(variable.second->description());
      output.push_back(L"");
      output.push_back(MakeNonNullShared<Line>(
          LineBuilder(Append(NewLazyString(L"* Value: "),
                             NewLazyString(
                                 print(source.Read(&variable.second.value())))))
              .Build()));
      output.push_back(MakeNonNullShared<Line>(
          LineBuilder(
              Append(NewLazyString(L"* Default: "),
                     NewLazyString(print(variable.second->default_value()))))
              .Build()));

      if (!variable.second->key().empty()) {
        output.push_back(L"* Related commands: `v" + variable.second->key() +
                         L"`");
      }
      output.push_back(L"");
    }
    output.push_back(L"");
  }

  static void CommandLineVariables(MutableLineSequence& output) {
    StartSection(L"## Command line arguments", output);
    using command_line_arguments::Handler;
    auto handlers = CommandLineArgs();
    for (auto& h : handlers) {
      StartSection(L"### " + h.aliases()[0], output);
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
      output.push_back(h.help());
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
               HelpCommand::GenerateContents(commands.ptr().value(),
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
