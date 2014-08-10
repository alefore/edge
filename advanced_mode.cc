#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>

#include "advanced_mode.h"
#include "command.h"
#include "command_mode.h"
#include "editor.h"
#include "help_command.h"
#include "map_mode.h"

namespace afc {
namespace editor {

using std::cerr;
using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

class CloseCurrentBuffer : public Command {
  const string Description() {
    return "closes the current buffer (without saving)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }

    editor_state->screen_needs_redraw = true;
    editor_state->buffers.erase(editor_state->current_buffer);
    editor_state->current_buffer = editor_state->buffers.begin();
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }
};

class SaveCurrentBuffer : public Command {
  const string Description() {
    return "saves the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }
    const auto& buffer = editor_state->get_current_buffer();
    string tmp_path = editor_state->current_buffer->first + ".tmp";
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1) {
      cerr << tmp_path << ": open failed: " << strerror(errno);
      exit(-1);
    }
    for (const auto& line : buffer->contents) {
      const auto& str = *line->contents;
      char* tmp = static_cast<char*>(malloc(str.size() + 1));
      strcpy(tmp, str.ToString().c_str());
      tmp[str.size()] = '\n';
      if (write(fd, tmp, str.size() + 1) == -1) {
        cerr << tmp_path << ": write failed: " << fd << ": " << strerror(errno);
        exit(-1);
      }
      free(tmp);
    }
    close(fd);
    rename(tmp_path.c_str(), editor_state->current_buffer->first.c_str());

    editor_state->status = "Saved: " + editor_state->current_buffer->first;
    editor_state->current_buffer = editor_state->buffers.begin();
    editor_state->mode = std::move(NewCommandMode());
  }
};

static const map<int, Command*>& GetAdvancedModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('c', new CloseCurrentBuffer()));
    output.insert(make_pair('s', new SaveCurrentBuffer()));
    output.insert(make_pair('?', NewHelpCommand(output, "advance command mode").release()));
  }
  return output;
}

unique_ptr<EditorMode> NewAdvancedMode() {
  unique_ptr<MapMode> mode(new MapMode(GetAdvancedModeMap()));
  return std::move(mode);
}

}  // namespace afc
}  // namespace editor
