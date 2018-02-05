#include "help_command.h"

#include <cassert>
#include <memory>
#include <map>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::shared_ptr;

namespace {
wstring DescribeSequence(const vector<wint_t> input) {
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
  HelpCommand(const map<vector<wint_t>, Command*>& commands,
              const wstring& mode_description)
      : commands_(commands), mode_description_(mode_description) {
    for (auto& it : commands_) {
      DCHECK(it.second);
    }
  }

  const wstring Description() {
    return L"shows help about commands.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    const wstring name = L"- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      shared_ptr<OpenBuffer> buffer(new OpenBuffer(editor_state, name));
      buffer->AppendToLastLine(
          editor_state,
          NewCopyString(L"Help: " + mode_description_));
      for (const auto& it : commands_) {
        buffer->AppendLine(editor_state, std::move(NewCopyString(
          DescribeSequence(it.first) + L" - " + it.second->Description())));
      }

      DescribeVariables(editor_state, L"bool", buffer.get(),
                        OpenBuffer::BoolStruct(),
                        [](const bool& value) {
                          return value ? L"true" : L"false";
                        });
      DescribeVariables(editor_state, L"string", buffer.get(),
                        OpenBuffer::StringStruct(),
                        [](const std::wstring& value) { return value; });
      DescribeVariables(editor_state, L"int", buffer.get(),
                        OpenBuffer::IntStruct(),
                        [](const int& value) { return std::to_wstring(value); });

      it.first->second = buffer;
    }
    it.first->second->set_current_position_line(0);

    editor_state->ScheduleRedraw();
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
  }

 private:
  template <typename T, typename C>
  void DescribeVariables(
      EditorState* editor_state, wstring type_name, OpenBuffer* buffer,
      EdgeStruct<T>* variables, /*std::function<std::wstring(const T&)>*/ C print) {
    buffer->AppendEmptyLine(editor_state);
    buffer->AppendLine(editor_state,
                       NewCopyString(L"Variables (" + type_name + L"):"));
    for (const auto& variable : variables->variables()) {
      buffer->AppendLine(editor_state,
                         NewCopyString(variable.second->name()));
      buffer->AppendLine(
          editor_state,
          StringAppend(NewCopyString(L"    "),
                       NewCopyString(variable.second->description())));
      buffer->AppendLine(editor_state,
          StringAppend(NewCopyString(L"    Default: "),
                       NewCopyString(print(variable.second->default_value()))));
    }
  }

  const map<vector<wint_t>, Command*> commands_;
  const wstring mode_description_;
};
}  // namespace

unique_ptr<Command> NewHelpCommand(
    const map<vector<wint_t>, Command*>& commands,
    const wstring& mode_description) {
  return unique_ptr<Command>(new HelpCommand(commands, mode_description));
}

}  // namespace editor
}  // namespace afc
