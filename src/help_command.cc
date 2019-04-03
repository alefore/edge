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

  const wstring Description() { return L"shows help about commands."; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto original_buffer = editor_state->current_buffer()->second;
    const wstring name = L"- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      auto buffer = std::make_shared<OpenBuffer>(editor_state, name);
      buffer->AppendToLastLine(editor_state,
                               NewCopyString(L"Help: " + mode_description_));
      std::map<wstring, Command*> descriptions = commands_->Coallesce();
      for (const auto& it : descriptions) {
        buffer->AppendLine(editor_state,
                           NewCopyString(DescribeSequence(it.first) + L" - " +
                                         it.second->Description()));
      }

      ShowEnvironment(editor_state, original_buffer.get(), buffer.get());

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
  void ShowEnvironment(EditorState* editor_state, OpenBuffer* original_buffer,
                       OpenBuffer* output) {
    output->AppendEmptyLine(editor_state);

    auto environment = original_buffer->environment();
    CHECK(environment != nullptr);

    output->AppendLine(editor_state, NewCopyString(L"Environment types:"));
    environment->ForEachType([editor_state, output](const wstring& name,
                                                    ObjectType* type) {
      CHECK(type != nullptr);
      output->AppendLine(editor_state,
                         StringAppend(NewCopyString(L"  "), NewCopyString(name),
                                      NewCopyString(L":")));
      type->ForEachField([editor_state, output](const wstring& field_name,
                                                Value* value) {
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
            StringAppend(NewCopyString(L"      ."), NewCopyString(field_name),
                         NewCopyString(padding),
                         NewCopyString(FromByteString(value_stream.str()))));
      });
      output->AppendEmptyLine(editor_state);
    });

    output->AppendLine(editor_state, NewCopyString(L"Environment symbols:"));
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
  }

  template <typename T, typename C>
  void DescribeVariables(EditorState* editor_state, wstring type_name,
                         OpenBuffer* buffer, EdgeStruct<T>* variables,
                         /*std::function<std::wstring(const T&)>*/ C print) {
    buffer->AppendEmptyLine(editor_state);
    buffer->AppendLine(editor_state,
                       NewCopyString(L"Variables (" + type_name + L"):"));
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
