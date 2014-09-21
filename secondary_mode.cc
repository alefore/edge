#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>

extern "C" {
#include <libgen.h>
#include <sys/socket.h>
}

#include "advanced_mode.h"
#include "char_buffer.h"
#include "command.h"
#include "command_mode.h"
#include "editor.h"
#include "file_link_mode.h"
#include "help_command.h"
#include "line_prompt_mode.h"
#include "map_mode.h"
#include "run_command_handler.h"

namespace afc {
namespace editor {

namespace {

class RecordCommand : public Command {
  const string Description() {
    return "starts/stops recording a transformation";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer();
    if (buffer->second->HasTransformationStack()) {
      buffer->second->PopTransformationStack();
      editor_state->SetStatus("Recording: stop");
    } else {
      buffer->second->PushTransformationStack();
      editor_state->SetStatus("Recording: start");
    }
    editor_state->ResetMode();
  }
};

class RestoreCommandMode : public Command {
  const string Description() {
    return "restores command mode";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->ResetMode();
  }
};

}

using std::cerr;
using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

static const map<int, Command*>& GetSecondaryModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('r', new RecordCommand()));
    output.insert(make_pair('?',
        NewHelpCommand(output, "secondary command mode").release()));
  }
  return output;
}

unique_ptr<EditorMode> NewSecondaryMode() {
  static auto default_command = new RestoreCommandMode();
  unique_ptr<MapMode> mode(new MapMode(GetSecondaryModeMap(), default_command));
  return std::move(mode);
}

}  // namespace afc
}  // namespace editor
