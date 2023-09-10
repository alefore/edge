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

namespace {
std::wstring DescribeSequence(std::wstring input) {
  std::wstring output = L"`";
  for (auto& c : input) {
    switch (c) {
      case '\t':
        output += L"Tab";
        break;
      case '\n':
        output.push_back(L'↩');
        break;
      case Terminal::ESCAPE:
        output += L"Esc";
        break;
      case Terminal::DOWN_ARROW:
        output += L"↓";
        break;
      case Terminal::UP_ARROW:
        output += L"↑";
        break;
      case Terminal::LEFT_ARROW:
        output += L"←";
        break;
      case Terminal::RIGHT_ARROW:
        output += L"→";
        break;
      case Terminal::BACKSPACE:
        output += L"← Backspace";
        break;
      case Terminal::PAGE_DOWN:
        output += L"PgDn";
        break;
      case Terminal::PAGE_UP:
        output += L"PgUp";
        break;
      case Terminal::CTRL_A:
        output += L"^a";
        break;
      case Terminal::CTRL_D:
        output += L"^d";
        break;
      case Terminal::CTRL_E:
        output += L"^e";
        break;
      case Terminal::CTRL_K:
        output += L"^k";
        break;
      case Terminal::CTRL_L:
        output += L"^l";
        break;
      case Terminal::CTRL_U:
        output += L"^u";
        break;
      case Terminal::CTRL_V:
        output += L"^v";
        break;
      case Terminal::DELETE:
        output += L"Delete";
        break;
      default:
        output.push_back(static_cast<wchar_t>(c));
    }
  }
  return output + L"`";
}

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

    editor_state_.buffers()->insert_or_assign(name, buffer_root);
    editor_state_.AddBuffer(buffer_root, BuffersList::AddBufferType::kVisit);
    editor_state_.ResetRepetitions();
  }

  static LineSequence GenerateContents(const MapModeCommands& commands,
                                       const OpenBuffer& buffer) {
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

 private:
  static void StartSection(std::wstring section, MutableLineSequence& buffer) {
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
        output.push_back(L"* " + DescribeSequence(input) + L" - " +
                         command->Description());
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
          LineBuilder(
              Append(NewLazyString(L"* Value: "),
                     NewLazyString(print(source.Read(variable.second.get())))))
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
               auto buffer = NewBufferForTests();
               MapModeCommands commands(buffer.ptr()->editor());
               HelpCommand::GenerateContents(commands, buffer.ptr().value());
             }},
    });
}  // namespace

NonNull<std::unique_ptr<Command>> NewHelpCommand(
    EditorState& editor_state, const MapModeCommands& commands,
    const std::wstring& mode_description) {
  return MakeNonNullUnique<HelpCommand>(editor_state, commands,
                                        mode_description);
}

}  // namespace afc::editor
