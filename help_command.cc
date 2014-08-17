#include "help_command.h"

#include <cassert>
#include <memory>
#include <map>

#include "char_buffer.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::shared_ptr;

class HelpCommand : public Command {
 public:
  HelpCommand(const map<int, Command*>& commands,
              const string& mode_description)
      : commands_(commands), mode_description_(mode_description) {}

  const string Description() {
    return "shows help about commands.";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    const string name = "- help: " + mode_description_;
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      shared_ptr<OpenBuffer> buffer(new OpenBuffer(name));
      buffer->AppendLine(
          std::move(NewCopyString("Help: " + mode_description_)));
      for (const auto& it : commands_) {
        buffer->AppendLine(std::move(NewCopyString(
          (it.first == '\n' ? "RET" : string(1, static_cast<char>(it.first)))
          + " - " + it.second->Description())));
      }
      it.first->second = buffer;
    }
    it.first->second->set_current_position_line(0);

    editor_state->ScheduleRedraw();
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
  }

 private:
  const map<int, Command*> commands_;
  const string mode_description_;
};

unique_ptr<Command> NewHelpCommand(
    const map<int, Command*>& commands,
    const string& mode_description) {
  return unique_ptr<Command>(new HelpCommand(commands, mode_description));
}

}  // namespace editor
}  // namespace afc
