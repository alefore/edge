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
  const wstring Description() {
    return L"starts/stops recording a transformation";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer();
    if (buffer->second->HasTransformationStack()) {
      buffer->second->PopTransformationStack();
      editor_state->SetStatus(L"Recording: stop");
    } else {
      buffer->second->PushTransformationStack();
      editor_state->SetStatus(L"Recording: start");
    }
    editor_state->ResetMode();
  }
};

class RestoreCommandMode : public Command {
  const wstring Description() {
    return L"restores command mode";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ResetMode();
  }
};

}

using std::cerr;
using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

static const map<wchar_t, Command*>& GetSecondaryModeMap() {
  static map<wchar_t, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('r', new RecordCommand()));
    output.insert(make_pair('?',
        NewHelpCommand(output, L"secondary command mode").release()));
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
