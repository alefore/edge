#include "help_command.h"

#include <map>
#include <memory>

#include <glog/logging.h>

#include "buffer_variables.h"
#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"

namespace afc {
namespace editor {

using std::map;
using std::shared_ptr;
using std::unique_ptr;

namespace {
wstring DescribeSequence(wstring input) {
  wstring output;
  for (wint_t c : input) {
    if (c == '\n') {
      output.push_back(L'↩');
    } else {
      output.push_back(static_cast<wchar_t>(c));
    }
  }
  return output;
}

class HelpCommand : public Command {
 public:
  HelpCommand(const MapModeCommands* commands, const wstring& mode_description)
      : commands_(commands), mode_description_(mode_description) {}

  const wstring Description() { return L"shows documentation."; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto original_buffer = editor_state->current_buffer()->second;
    const wstring name = L"- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      auto buffer = std::make_shared<OpenBuffer>(editor_state, name);
      buffer->set_string_variable(buffer_variables::tree_parser(), L"md");

      buffer->AppendToLastLine(editor_state, NewCopyString(L"## Edge - Help"));
      buffer->AppendEmptyLine(editor_state);

      ShowCommands(editor_state, buffer.get());
      ShowEnvironment(editor_state, original_buffer.get(), buffer.get());

      StartSection(L"### Buffer Variables", editor_state, buffer.get());
      DescribeVariables(
          editor_state, L"bool", buffer.get(), buffer_variables::BoolStruct(),
          [](const bool& value) { return value ? L"true" : L"false"; });
      DescribeVariables(editor_state, L"string", buffer.get(),
                        buffer_variables::StringStruct(),
                        [](const std::wstring& value) { return value; });
      DescribeVariables(
          editor_state, L"int", buffer.get(), buffer_variables::IntStruct(),
          [](const int& value) { return std::to_wstring(value); });

      it.first->second = buffer;
    }
    it.first->second->set_current_position_line(0);
    it.first->second->ResetMode();

    editor_state->ScheduleRedraw();
    editor_state->ResetRepetitions();
  }

 private:
  void StartSection(const wstring& section, EditorState* editor_state,
                    OpenBuffer* buffer) {
    buffer->AppendLine(editor_state, NewCopyString(section));
    buffer->AppendEmptyLine(editor_state);
  }

  void ShowCommands(EditorState* editor_state, OpenBuffer* output_buffer) {
    StartSection(L"### Commands", editor_state, output_buffer);
    for (const auto& it : commands_->Coallesce()) {
      output_buffer->AppendLine(
          editor_state, NewCopyString(DescribeSequence(it.first) + L" - " +
                                      it.second->Description()));
    }
    output_buffer->AppendEmptyLine(editor_state);
  }

  void ShowEnvironment(EditorState* editor_state, OpenBuffer* original_buffer,
                       OpenBuffer* output) {
    StartSection(L"### Environment", editor_state, output);

    auto environment = original_buffer->environment();
    CHECK(environment != nullptr);

    StartSection(L"#### Types & methods", editor_state, output);
    environment->ForEachType([&](const wstring& name, ObjectType* type) {
      CHECK(type != nullptr);
      StartSection(L"##### " + name, editor_state, output);
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
            StringAppend(NewCopyString(field_name), NewCopyString(padding),
                         NewCopyString(FromByteString(value_stream.str()))));
      });
      output->AppendEmptyLine(editor_state);
    });
    output->AppendEmptyLine(editor_state);

    StartSection(L"#### Variables", editor_state, output);

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
          StringAppend(NewCopyString(L"  "), NewCopyString(name),
                       NewCopyString(padding),
                       NewCopyString(FromByteString(value_stream.str()))));
    });
    output->AppendEmptyLine(editor_state);
  }

  template <typename T, typename C>
  void DescribeVariables(EditorState* editor_state, wstring type_name,
                         OpenBuffer* buffer, EdgeStruct<T>* variables,
                         /*std::function<std::wstring(const T&)>*/ C print) {
    StartSection(L"#### " + type_name, editor_state, buffer);
    for (const auto& variable : variables->variables()) {
      buffer->AppendLine(editor_state, NewCopyString(variable.second->name()));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewCopyString(L"    "),
                       NewCopyString(variable.second->description())));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewCopyString(L"    Default: "),
                       NewCopyString(print(variable.second->default_value()))));
    }
    buffer->AppendEmptyLine(editor_state);
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
