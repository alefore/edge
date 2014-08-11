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
    auto it = editor_state->buffers.insert(
        make_pair("Help: " + mode_description_, nullptr));
    editor_state->current_buffer = it.first;
    if (it.second) {
      it.first->second.reset(Load().release());
    }
    it.first->second->current_position_line = 0;

    editor_state->screen_needs_redraw = true;
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }

 private:
  unique_ptr<OpenBuffer> Load() {
    unique_ptr<OpenBuffer> buffer(new OpenBuffer());
    {
      unique_ptr<Line> line(new Line);
      line->contents.reset(
          NewCopyString("Help: " + mode_description_).release());
      buffer->contents.push_back(std::move(line));
    }
    for (const auto& it : commands_) {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyString(
          (it.first == '\n' ? "RET" : string(1, static_cast<char>(it.first)))
          + " - " + it.second->Description()).release());
      buffer->contents.push_back(std::move(line));
    }
    return std::move(buffer);
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
