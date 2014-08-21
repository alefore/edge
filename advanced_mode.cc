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
    editor_state->ResetMode();
  }
};

class OpenDirectory : public Command {
  const string Description() {
    return "opens a view of the current directory";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    string path;
    if (!editor_state->has_current_buffer()) {
      path = ".";
    } else {
      char* tmp = strdup(editor_state->current_buffer()->first.c_str());
      path = dirname(tmp);
      free(tmp);
    }
    editor_state->PushCurrentPosition();
    unique_ptr<EditorMode> loader(
        NewFileLinkMode(editor_state, path, 0, false));
    loader->ProcessInput('\n', editor_state);
  }
};

// TODO: This should be a method of EditorState.
static void CloseBuffer(
    EditorState* editor_state,
    map<string, shared_ptr<OpenBuffer>>::iterator buffer) {
  editor_state->ScheduleRedraw();
  map<string, shared_ptr<OpenBuffer>>::iterator it;
  if (editor_state->buffers()->size() == 1) {
    it = editor_state->buffers()->end();
  } else {
    it = buffer;
    if (it == editor_state->buffers()->begin()) {
      it = editor_state->buffers()->end();
    }
    it--;
    assert(it != buffer);
    assert(it != editor_state->buffers()->end());
    it->second->Enter(editor_state);
  }
  editor_state->buffers()->erase(buffer);
  if (buffer == editor_state->current_buffer()) {
    editor_state->set_current_buffer(it);
  }
}

class CloseCurrentBuffer : public Command {
  const string Description() {
    return "closes the current buffer (without saving)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    CloseBuffer(editor_state, editor_state->current_buffer());
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
  }
};

class SaveCurrentBuffer : public Command {
  const string Description() {
    return "saves the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->current_buffer()->second->Save(editor_state);
    editor_state->ResetMode();
  }
};

void OpenFileHandler(const string& name, EditorState* editor_state) {
  unique_ptr<EditorMode> mode(NewFileLinkMode(editor_state, name, 0, false));
  editor_state->PushCurrentPosition();
  mode->ProcessInput('\n', editor_state);
}

void SetVariableHandler(const string& name, EditorState* editor_state) {
  editor_state->ResetMode();
  {
    const EdgeVariable<string>* var = OpenBuffer::StringStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      Prompt(
          editor_state,
          name + " := ",
          editor_state->current_buffer()->second->read_string_variable(var),
          [var](const string& input, EditorState* editor_state) {
            if (editor_state->has_current_buffer()) {
              editor_state->current_buffer()
                  ->second->set_string_variable(var, input);
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          });
      return;
    }
  }
  {
    auto var = OpenBuffer::BoolStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      auto buffer = editor_state->current_buffer()->second;
      buffer->toggle_bool_variable(var);
      editor_state->SetStatus(
          name + " := " + (buffer->read_bool_variable(var) ? "ON" : "OFF"));
      return;
    }
  }
  editor_state->SetStatus("Unknown variable: " + name);
}

class ActivateBufferLineCommand : public EditorMode {
 public:
  ActivateBufferLineCommand(const string& name) : name_(name) {}

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        {
          editor_state->PushCurrentPosition();
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus("Buffer not found: " + name_);
            return;
          }
          editor_state->set_current_buffer(it);
          it->second->Enter(editor_state);
          editor_state->ScheduleRedraw();
          editor_state->ResetStatus();
          editor_state->ResetMode();
          break;
        }
      case 'd':
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          CloseBuffer(editor_state, it);
          break;
        }
    }
  }

 private:
  const string name_;
};

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(const string& name) : OpenBuffer(name) {
    set_bool_variable(variable_atomic_lines(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->contents()->clear();
    AppendLine(std::move(NewCopyString("Open Buffers:")));
    for (const auto& it : *editor_state->buffers()) {
      string flags(it.second->FlagsString());
      auto name = NewCopyString(it.first + (flags.empty() ? "" : "  ") + flags);
      target->AppendLine(std::move(name))
          ->activate.reset(new ActivateBufferLineCommand(it.first));
    }
    editor_state->ScheduleRedraw();
  }
};

class ListBuffers : public Command {
 public:
  const string Description() {
    return "lists all open buffers";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    const string name = "- open buffers";
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->PushCurrentPosition();
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      it.first->second.reset(new ListBuffersBuffer(name));
      it.first->second->set_bool_variable(
          OpenBuffer::variable_reload_on_enter(), true);
    }
    it.first->second->Reload(editor_state);
    editor_state->ScheduleRedraw();
    editor_state->ResetStatus();
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
  }

 private:
};

class ReloadBuffer : public Command {
 public:
  const string Description() {
    return "reloads the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->has_current_buffer()) {
      auto buffer = editor_state->current_buffer();
      buffer->second->Reload(editor_state);
    }
    editor_state->ResetMode();
  }
};

class SendEndOfFile : public Command {
 public:
  const string Description() {
    return "stops writing to a subprocess (effectively sending EOF).";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->ResetMode();
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    if (buffer->fd() == -1) {
      editor_state->SetStatus("No active subprocess for current buffer.");
      return;
    }
    if (shutdown(buffer->fd(), SHUT_WR) == -1) {
      editor_state->SetStatus("shutdown(SHUT_WR) failed: " + string(strerror(errno)));
      return;
    }
    editor_state->SetStatus("shutdown sent");
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
    output.insert(make_pair('e', new SendEndOfFile()));
    output.insert(make_pair(
        'o',
        NewLinePromptCommand("<", "loads a file", OpenFileHandler).release()));
    output.insert(make_pair(
        'F',
        NewLinePromptCommand(
            "...$ ",
            "forks a command for each line in the current buffer",
            RunMultipleCommandsHandler).release()));
    output.insert(make_pair('f', NewForkCommand().release()));
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
