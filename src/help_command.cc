#include "src/help_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/terminal.h"

namespace afc {
namespace editor {

using std::map;
using std::shared_ptr;
using std::unique_ptr;

namespace {
wstring DescribeSequence(wstring input) {
  wstring output = L"`";
  for (wint_t c : input) {
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
  HelpCommand(const MapModeCommands* commands, const wstring& mode_description)
      : commands_(commands), mode_description_(mode_description) {}

  wstring Description() const override { return L"Shows documentation."; }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto original_buffer = editor_state->current_buffer();
    const wstring name = L"- help: " + mode_description_;

    auto buffer = std::make_shared<OpenBuffer>(editor_state, name);
    buffer->Set(buffer_variables::tree_parser, L"md");
    buffer->Set(buffer_variables::wrap_from_content, true);
    buffer->Set(buffer_variables::allow_dirty_delete, true);
    buffer->Set(buffer_variables::show_in_buffers_list, false);

    buffer->AppendToLastLine(NewLazyString(L"# Edge - Help"));
    buffer->AppendEmptyLine();

    ShowCommands(buffer.get());
    ShowEnvironment(original_buffer.get(), buffer.get());

    StartSection(L"## Buffer Variables", buffer.get());
    buffer->AppendLine(
        NewLazyString(L"The following are all the buffer variables defined for "
                      L"your buffer."));
    buffer->AppendEmptyLine();

    auto variable_commands = commands_->GetVariableCommands();

    DescribeVariables(
        L"bool", *original_buffer, buffer.get(), buffer_variables::BoolStruct(),
        variable_commands,
        [](const bool& value) { return value ? L"true" : L"false"; });
    DescribeVariables(
        L"string", *original_buffer, buffer.get(),
        buffer_variables::StringStruct(), variable_commands,
        [](const std::wstring& value) { return L"`" + value + L"`"; });
    DescribeVariables(L"int", *original_buffer, buffer.get(),
                      buffer_variables::IntStruct(), variable_commands,
                      [](const int& value) { return std::to_wstring(value); });

    CommandLineVariables(buffer.get());
    buffer->set_current_position_line(LineNumber(0));
    buffer->ResetMode();

    editor_state->buffers()->insert(make_pair(name, buffer));
    editor_state->set_current_buffer(buffer);

    editor_state->ScheduleRedraw();
    editor_state->ResetRepetitions();
  }

 private:
  void StartSection(wstring section, OpenBuffer* buffer) {
    buffer->AppendLine(NewLazyString(std::move(section)));
    buffer->AppendEmptyLine();
  }

  void ShowCommands(OpenBuffer* output_buffer) {
    StartSection(L"## Commands", output_buffer);

    output_buffer->AppendLine(
        NewLazyString(L"The following is a list of all commands available in "
                      L"your buffer, grouped by category."));
    output_buffer->AppendEmptyLine();

    for (const auto& category : commands_->Coallesce()) {
      StartSection(L"### " + category.first, output_buffer);
      for (const auto& it : category.second) {
        output_buffer->AppendLine(
            NewLazyString(L"* " + DescribeSequence(it.first) + L" - " +
                          it.second->Description()));
      }
      output_buffer->AppendEmptyLine();
    }
  }

  void ShowEnvironment(OpenBuffer* original_buffer, OpenBuffer* output) {
    StartSection(L"## Environment", output);

    auto environment = original_buffer->environment();
    CHECK(environment != nullptr);

    StartSection(L"### Types & methods", output);

    output->AppendLine(NewLazyString(
        L"This section contains a list of all types available to Edge "
        L"extensions running in your buffer. For each, a list of all their "
        L"available methods is given."));
    output->AppendEmptyLine();

    environment->ForEachType([&](const wstring& name, ObjectType* type) {
      CHECK(type != nullptr);
      StartSection(L"#### " + name, output);
      type->ForEachField([&](const wstring& field_name, Value* value) {
        CHECK(value != nullptr);
        std::stringstream value_stream;
        value_stream << *value;
        const static int kPaddingSize = 40;
        wstring padding(field_name.size() > kPaddingSize
                            ? 0
                            : kPaddingSize - field_name.size(),
                        L' ');
        output->AppendLine(StringAppend(
            NewLazyString(L"* `"), NewLazyString(field_name),
            NewLazyString(L"`" + std::move(padding) + L"`"),
            NewLazyString(FromByteString(value_stream.str()) + L"`")));
      });
      output->AppendEmptyLine();
    });
    output->AppendEmptyLine();

    StartSection(L"### Variables", output);

    output->AppendLine(NewLazyString(
        L"The following are all variables defined in the environment "
        L"associated with your buffer, and thus available to "
        L"extensions."));
    output->AppendEmptyLine();

    environment->ForEach([output](const wstring& name, Value* value) {
      const static int kPaddingSize = 40;
      wstring padding(
          name.size() >= kPaddingSize ? 1 : kPaddingSize - name.size(), L' ');

      std::stringstream value_stream;
      if (value == nullptr) {
        value_stream << "(nullptr)";
      } else {
        value_stream << *value;
      }

      output->AppendLine(StringAppend(
          NewLazyString(L"* `"), NewLazyString(name),
          NewLazyString(L"`" + std::move(padding) + L"`"),
          NewLazyString(FromByteString(value_stream.str()) + L"`")));
    });
    output->AppendEmptyLine();
  }

  template <typename T, typename C>
  void DescribeVariables(
      wstring type_name, const OpenBuffer& source, OpenBuffer* buffer,
      EdgeStruct<T>* variables,
      const std::map<std::wstring, std::set<std::wstring>>& variable_commands,
      /*std::function<std::wstring(const T&)>*/ C print) {
    StartSection(L"### " + type_name, buffer);
    for (const auto& variable : variables->variables()) {
      buffer->AppendLine(StringAppend(NewLazyString(L"#### "),
                                      NewLazyString(variable.second->name())));
      buffer->AppendEmptyLine();
      buffer->AppendLazyString(NewLazyString(variable.second->description()));
      buffer->AppendEmptyLine();
      buffer->AppendLazyString(StringAppend(
          NewLazyString(L"* Value: "),
          NewLazyString(print(source.Read(variable.second.get())))));
      buffer->AppendLazyString(
          StringAppend(NewLazyString(L"* Default: "),
                       NewLazyString(print(variable.second->default_value()))));

      auto commands = variable_commands.find(variable.second->name());
      if (commands != variable_commands.end()) {
        buffer->AppendLazyString(NewLazyString(L"* Related commands:"));
        for (auto& it : commands->second) {
          buffer->AppendLazyString(NewLazyString(L"  * `" + it + L"`"));
        }
      }
      buffer->AppendEmptyLine();
    }
    buffer->AppendEmptyLine();
  }

  void CommandLineVariables(OpenBuffer* buffer) {
    StartSection(L"## Command line arguments", buffer);
    using command_line_arguments::Handler;
    auto handlers = command_line_arguments::Handlers();
    for (auto& h : handlers) {
      StartSection(L"### " + h.aliases()[0], buffer);
      switch (h.argument_type()) {
        case Handler::VariableType::kRequired:
          buffer->AppendLine(NewLazyString(L"Required argument: " +
                                           h.argument() + L": " +
                                           h.argument_description()));
          buffer->AppendEmptyLine();
          break;

        case Handler::VariableType::kOptional:
          buffer->AppendLine(NewLazyString(L"Optional argument: " +
                                           h.argument() + L": " +
                                           h.argument_description()));
          buffer->AppendEmptyLine();
          break;

        case Handler::VariableType::kNone:
          break;
      }
      buffer->AppendLazyString(NewLazyString(h.help()));
      buffer->AppendEmptyLine();
    }
  }
  const MapModeCommands* const commands_;
  const wstring mode_description_;
};
}  // namespace

unique_ptr<Command> NewHelpCommand(const MapModeCommands* commands,
                                   const wstring& mode_description) {
  return std::make_unique<HelpCommand>(commands, mode_description);
}

}  // namespace editor
}  // namespace afc
