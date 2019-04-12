#include "help_command.h"

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
  wstring output;
  if (!input.empty() && input[0] == '#') {
    output.push_back(L' ');  // It isn't a header!
  }
  for (wint_t c : input) {
    switch (c) {
      case '\n':
        output.push_back(L'↩');
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
      case Terminal::CTRL_U:
        output += L"^u";
        break;
      default:
        output.push_back(static_cast<wchar_t>(c));
    }
  }
  return output;
}

class HelpCommand : public Command {
 public:
  HelpCommand(const MapModeCommands* commands, const wstring& mode_description)
      : commands_(commands), mode_description_(mode_description) {}

  wstring Description() const override { return L"Shows documentation."; }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto original_buffer = editor_state->current_buffer()->second;
    const wstring name = L"- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);

    auto buffer = std::make_shared<OpenBuffer>(editor_state, name);
    buffer->Set(buffer_variables::tree_parser(), L"md");
    buffer->Set(buffer_variables::allow_dirty_delete(), true);
    buffer->Set(buffer_variables::show_in_buffers_list(), false);

    buffer->AppendToLastLine(editor_state, NewLazyString(L"# Edge - Help"));
    buffer->AppendEmptyLine(editor_state);

    ShowCommands(editor_state, buffer.get());
    ShowEnvironment(editor_state, original_buffer.get(), buffer.get());

    StartSection(L"## Buffer Variables", editor_state, buffer.get());
    buffer->AppendLine(
        editor_state,
        NewLazyString(
            L"The following are all the buffer variables defined for your "
            L"buffer."));
    buffer->AppendEmptyLine(editor_state);

    DescribeVariables(
        editor_state, L"bool", buffer.get(), buffer_variables::BoolStruct(),
        [](const bool& value) { return value ? L"true" : L"false"; });
    DescribeVariables(
        editor_state, L"string", buffer.get(), buffer_variables::StringStruct(),
        [](const std::wstring& value) { return L"\"" + value + L"\""; });
    DescribeVariables(editor_state, L"int", buffer.get(),
                      buffer_variables::IntStruct(),
                      [](const int& value) { return std::to_wstring(value); });

    CommandLineVariables(editor_state, buffer.get());
    buffer->set_current_position_line(0);
    buffer->ResetMode();

    it.first->second = buffer;

    editor_state->ScheduleRedraw();
    editor_state->ResetRepetitions();
  }

 private:
  void StartSection(wstring section, EditorState* editor_state,
                    OpenBuffer* buffer) {
    buffer->AppendLine(editor_state, NewLazyString(std::move(section)));
    buffer->AppendEmptyLine(editor_state);
  }

  void ShowCommands(EditorState* editor_state, OpenBuffer* output_buffer) {
    StartSection(L"## Commands", editor_state, output_buffer);

    output_buffer->AppendLine(
        editor_state,
        NewLazyString(L"The following is a list of all commands available in "
                      L"your buffer, grouped by category."));
    output_buffer->AppendEmptyLine(editor_state);

    for (const auto& category : commands_->Coallesce()) {
      StartSection(L"### " + category.first, editor_state, output_buffer);
      for (const auto& it : category.second) {
        output_buffer->AppendLine(
            editor_state, NewLazyString(DescribeSequence(it.first) + L" - " +
                                        it.second->Description()));
      }
      output_buffer->AppendEmptyLine(editor_state);
    }
  }

  void ShowEnvironment(EditorState* editor_state, OpenBuffer* original_buffer,
                       OpenBuffer* output) {
    StartSection(L"## Environment", editor_state, output);

    auto environment = original_buffer->environment();
    CHECK(environment != nullptr);

    StartSection(L"### Types & methods", editor_state, output);

    output->AppendLine(
        editor_state,
        NewLazyString(
            L"This section contains a list of all types available to Edge "
            L"extensions running in your buffer. For each, a list of all their "
            L"available methods is given."));
    output->AppendEmptyLine(editor_state);

    environment->ForEachType([&](const wstring& name, ObjectType* type) {
      CHECK(type != nullptr);
      StartSection(L"#### " + name, editor_state, output);
      type->ForEachField([&](const wstring& field_name, Value* value) {
        CHECK(value != nullptr);
        std::stringstream value_stream;
        value_stream << *value;
        const static int kPaddingSize = 40;
        wstring padding(field_name.size() > kPaddingSize
                            ? 0
                            : kPaddingSize - field_name.size(),
                        L' ');
        output->AppendLine(
            editor_state,
            StringAppend(NewLazyString(field_name),
                         NewLazyString(std::move(padding)),
                         NewLazyString(FromByteString(value_stream.str()))));
      });
      output->AppendEmptyLine(editor_state);
    });
    output->AppendEmptyLine(editor_state);

    StartSection(L"### Variables", editor_state, output);

    output->AppendLine(
        editor_state,
        NewLazyString(
            L"The following are all variables defined in the environment "
            L"associated with your buffer, and thus available to "
            L"extensions."));
    output->AppendEmptyLine(editor_state);

    environment->ForEach([editor_state, output](const wstring& name,
                                                Value* value) {
      const static int kPaddingSize = 40;
      wstring padding(
          name.size() >= kPaddingSize ? 1 : kPaddingSize - name.size(), L' ');

      std::stringstream value_stream;
      if (value == nullptr) {
        value_stream << "(nullptr)";
      } else {
        value_stream << *value;
      }

      output->AppendLine(
          editor_state,
          StringAppend(NewLazyString(L"  "), NewLazyString(name),
                       NewLazyString(std::move(padding)),
                       NewLazyString(FromByteString(value_stream.str()))));
    });
    output->AppendEmptyLine(editor_state);
  }

  template <typename T, typename C>
  void DescribeVariables(EditorState* editor_state, wstring type_name,
                         OpenBuffer* buffer, EdgeStruct<T>* variables,
                         /*std::function<std::wstring(const T&)>*/ C print) {
    StartSection(L"### " + type_name, editor_state, buffer);
    for (const auto& variable : variables->variables()) {
      buffer->AppendLine(editor_state, NewLazyString(variable.second->name()));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewLazyString(L"    "),
                       NewLazyString(variable.second->description())));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewLazyString(L"    Default: "),
                       NewLazyString(print(variable.second->default_value()))));
    }
    buffer->AppendEmptyLine(editor_state);
  }

  void CommandLineVariables(EditorState* editor_state, OpenBuffer* buffer) {
    StartSection(L"## Command line arguments", editor_state, buffer);
    using command_line_arguments::Handler;
    auto handlers = command_line_arguments::Handlers();
    for (auto& h : handlers) {
      StartSection(L"### " + h.aliases()[0], editor_state, buffer);
      switch (h.argument_type()) {
        case Handler::VariableType::kRequired:
          buffer->AppendLine(
              editor_state,
              NewLazyString(L"Required argument: " + h.argument() + L": " +
                            h.argument_description()));
          buffer->AppendEmptyLine(editor_state);
          break;

        case Handler::VariableType::kOptional:
          buffer->AppendLine(
              editor_state,
              NewLazyString(L"Optional argument: " + h.argument() + L": " +
                            h.argument_description()));
          buffer->AppendEmptyLine(editor_state);
          break;

        case Handler::VariableType::kNone:
          break;
      }
      std::wstringstream help(h.help());
      std::wstring line;

      while (std::getline(help, line, L'\n')) {
        buffer->AppendLine(editor_state, NewLazyString(std::move(line)));
      }
      buffer->AppendEmptyLine(editor_state);
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
