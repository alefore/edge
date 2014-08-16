#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>

extern "C" {
#include <libgen.h>
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

using std::cerr;
using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

class RestoreCommandMode : public Command {
  const string Description() {
    return "restores command mode";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->mode = std::move(NewCommandMode());
  }
};

class OpenDirectory : public Command {
  const string Description() {
    return "opens a view of the current directory";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    string path;
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      path = ".";
    } else {
      char* tmp = strdup(editor_state->current_buffer->first.c_str());
      path = dirname(tmp);
      free(tmp);
    }
    editor_state->PushCurrentPosition();
    unique_ptr<EditorMode> loader(NewFileLinkMode(path, 0, false));
    loader->ProcessInput('\n', editor_state);
  }
};

class CloseCurrentBuffer : public Command {
  const string Description() {
    return "closes the current buffer (without saving)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }

    editor_state->screen_needs_redraw = true;
    map<string, shared_ptr<OpenBuffer>>::iterator it;
    if (editor_state->buffers.size() == 1) {
      it = editor_state->buffers.end();
    } else {
      it = editor_state->current_buffer;
      if (it == editor_state->buffers.begin()) {
        it = editor_state->buffers.end();
      }
      it--;
      assert(it != editor_state->current_buffer);
      assert(it != editor_state->buffers.end());
      it->second->Enter(editor_state);
    }
    editor_state->buffers.erase(editor_state->current_buffer);
    editor_state->current_buffer = it;
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
    editor_state->get_current_buffer()->Save(editor_state);
    editor_state->mode = std::move(NewCommandMode());
  }
};

void OpenFileHandler(const string& name, EditorState* editor_state) {
  unique_ptr<EditorMode> mode(NewFileLinkMode(name, 0, false));
  editor_state->PushCurrentPosition();
  mode->ProcessInput(0, editor_state);
}

void SetWordCharacters(const string& input, EditorState* editor_state) {
  editor_state->mode = NewCommandMode();
  if (editor_state->current_buffer == editor_state->buffers.end()) {
    return;
  }
  editor_state->get_current_buffer()->set_word_characters(input);
}

void SetVariableHandler(const string& name, EditorState* editor_state) {
  editor_state->mode = std::move(NewCommandMode());
  if (name == "reload_on_enter") {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }
    editor_state->get_current_buffer()->toggle_reload_on_enter();
  } else if (name == "diff") {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }
    editor_state->get_current_buffer()->toggle_diff();
  } else if (name == "word_characters") {
    unique_ptr<Command> command(NewLinePromptCommand(
        "word_characters: ", "", SetWordCharacters));
    command->ProcessInput('X', editor_state);
  } else {
    editor_state->status = "Unknown variable: " + name;
  }
}

class OpenBufferCommand : public EditorMode {
 public:
  OpenBufferCommand(const string& name) : name_(name) {}

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    auto it = editor_state->buffers.find(name_);
    if (it == editor_state->buffers.end()) {
      // TODO: Keep a function and re-open the buffer?
      editor_state->status = "Buffer not found: " + name_;
      return;
    }
    editor_state->current_buffer = it;
    it->second->Enter(editor_state);
    editor_state->screen_needs_redraw = true;
    editor_state->status = "";
    editor_state-> mode = std::move(NewCommandMode());
  }

 private:
  const string name_;
};

class ListBuffersBuffer : public OpenBuffer {
  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->contents()->clear();
    AppendLine(std::move(NewCopyString("Open Buffers:")));
    for (const auto& it : editor_state->buffers) {
      string flags(it.second->FlagsString());
      auto name = NewCopyString(it.first + (flags.empty() ? "" : "  ") + flags);
      target->AppendLine(std::move(name))
          ->activate.reset(new OpenBufferCommand(it.first));
    }
    editor_state->screen_needs_redraw = true;
  }
};

class ListBuffers : public Command {
 public:
  const string Description() {
    return "lists all open buffers";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    auto it = editor_state->buffers.insert(make_pair("- open buffers", nullptr));
    editor_state->PushCurrentPosition();
    editor_state->current_buffer = it.first;
    if (it.second) {
      it.first->second.reset(new ListBuffersBuffer());
      it.first->second->set_reload_on_enter(true);
    }
    it.first->second->Reload(editor_state);
    editor_state->screen_needs_redraw = true;
    editor_state->status = "";
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }

 private:
};

class ReloadBuffer : public Command {
 public:
  const string Description() {
    return "reloads the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->current_buffer != editor_state->buffers.end()) {
      auto buffer = editor_state->get_current_buffer();
      buffer->Reload(editor_state);
      buffer->set_modified(false);
      buffer->CheckPosition();
    }
    editor_state->mode = std::move(NewCommandMode());
  }
};

static const map<int, Command*>& GetAdvancedModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('d', new CloseCurrentBuffer()));
    output.insert(make_pair('w', new SaveCurrentBuffer()));
    output.insert(make_pair(
        'v',
        NewLinePromptCommand("var ", "assigns to a variable", SetVariableHandler).release()));
    output.insert(make_pair('.', new OpenDirectory()));
    output.insert(make_pair('l', new ListBuffers()));
    output.insert(make_pair('r', new ReloadBuffer()));
    output.insert(make_pair(
        'o',
        NewLinePromptCommand("<", "loads a file", OpenFileHandler).release()));
    output.insert(make_pair(
        'C',
        NewLinePromptCommand(
            "...$ ",
            "runs a command once for each line in the current buffer",
            RunMultipleCommandsHandler).release()));
    output.insert(
        make_pair('c', NewLinePromptCommand("$ ", "runs a command", RunCommandHandler).release()));
    output.insert(make_pair('?', NewHelpCommand(output, "advance command mode").release()));
  }
  return output;
}

unique_ptr<EditorMode> NewAdvancedMode() {
  static auto default_command = new RestoreCommandMode();
  unique_ptr<MapMode> mode(new MapMode(GetAdvancedModeMap(), default_command));
  return std::move(mode);
}

}  // namespace afc
}  // namespace editor
