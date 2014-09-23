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

string TrimWhitespace(const string& in) {
  size_t begin = in.find_first_not_of(" ", 0);
  if (begin == string::npos) {
    return "";
  }
  size_t end = in.find_last_not_of(" ", in.size());
  if (end == string::npos) {
    return "";
  }
  if (begin == 0 && end == in.size()) {
    return in;
  }
  return in.substr(begin, end - begin + 1);
}

}

using std::cerr;
using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

class Quit : public Command {
 public:
  const string Description() {
    return "quits";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->set_terminate(true);
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

class OpenDirectory : public Command {
  const string Description() {
    return "opens a view of the current directory";
  }

  void ProcessInput(int, EditorState* editor_state) {
    string path;
    if (!editor_state->has_current_buffer()) {
      path = ".";
    } else {
      char* tmp = strdup(editor_state->current_buffer()->first.c_str());
      path = dirname(tmp);
      free(tmp);
    }
    OpenFileOptions options;
    options.editor_state = editor_state;
    options.path = path;
    OpenFile(options);
    editor_state->ResetMode();
  }
};

class CloseCurrentBuffer : public Command {
  const string Description() {
    return "closes the current buffer (without saving)";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->CloseBuffer(editor_state->current_buffer());
    editor_state->set_structure(CHAR);
    editor_state->set_sticky_structure(false);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
    editor_state->ResetMode();
  }
};

class SaveCurrentBuffer : public Command {
  const string Description() {
    return "saves the current buffer";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->current_buffer()->second->Save(editor_state);
    editor_state->set_structure(CHAR);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
    editor_state->ResetMode();
  }
};

void OpenFileHandler(const string& name, EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.path = name;
  OpenFile(options);
}

void SetVariableHandler(const string& input_name, EditorState* editor_state) {
  editor_state->ResetMode();
  editor_state->ScheduleRedraw();
  string name = TrimWhitespace(input_name);
  if (name.empty()) { return; }
  {
    const EdgeVariable<string>* var =
        OpenBuffer::StringStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      Prompt(
          editor_state,
          name + " := ",
          "values",
          editor_state->current_buffer()->second->read_string_variable(var),
          [var](const string& input, EditorState* editor_state) {
            if (editor_state->has_current_buffer()) {
              editor_state->current_buffer()
                  ->second->set_string_variable(var, input);
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          },
          var->predictor());
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
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus("Buffer not found: " + name_);
            return;
          }
          editor_state->set_current_buffer(it);
          it->second->Enter(editor_state);
          editor_state->PushCurrentPosition();
          editor_state->ScheduleRedraw();
          editor_state->ResetStatus();
          editor_state->ResetMode();
          break;
        }
      case 'd':
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->CloseBuffer(it);
          break;
        }
    }
  }

 private:
  const string name_;
};

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(EditorState* editor_state, const string& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_atomic_lines(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents();
    AppendLine(editor_state, std::move(NewCopyString("Open Buffers:")));
    for (const auto& it : *editor_state->buffers()) {
      string flags(it.second->FlagsString());
      auto name = NewCopyString(it.first + (flags.empty() ? "" : "  ") + flags);
      target->AppendLine(editor_state, std::move(name));
      (*target->contents()->rbegin())->set_activate(
          unique_ptr<EditorMode>(new ActivateBufferLineCommand(it.first)));
    }
    editor_state->ScheduleRedraw();
  }
};

class ListBuffers : public Command {
 public:
  const string Description() {
    return "lists all open buffers";
  }

  void ProcessInput(int, EditorState* editor_state) {
    auto it = editor_state->buffers()->insert(
        make_pair(OpenBuffer::kBuffersName, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      it.first->second.reset(
          new ListBuffersBuffer(editor_state, OpenBuffer::kBuffersName));
      it.first->second->set_bool_variable(
          OpenBuffer::variable_reload_on_enter(), true);
    }
    editor_state->ResetStatus();
    it.first->second->Reload(editor_state);
    editor_state->PushCurrentPosition();
    editor_state->ScheduleRedraw();
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

  void ProcessInput(int, EditorState* editor_state) {
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

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->ResetMode();
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    if (buffer->fd() == -1) {
      editor_state->SetStatus("No active subprocess for current buffer.");
      return;
    }
    if (buffer->read_bool_variable(OpenBuffer::variable_pts())) {
      char str[1] = { 4 };
      if (write(buffer->fd(), str, sizeof(str)) == -1) {
        editor_state->SetStatus(
            "Sending EOF failed: " + string(strerror(errno)));
        return;
      }
      editor_state->SetStatus("EOF sent");
    } else {
      if (shutdown(buffer->fd(), SHUT_WR) == -1) {
        editor_state->SetStatus(
            "shutdown(SHUT_WR) failed: " + string(strerror(errno)));
        return;
      }
      editor_state->SetStatus("shutdown sent");
    }
  }
};

void RunCppCommandHandler(const string& name, EditorState* editor_state) {
  if (!editor_state->has_current_buffer()) { return; }
  editor_state->ResetMode();
  editor_state->current_buffer()->second->EvaluateString(editor_state, name);
}

class RunCppCommand : public Command {
 public:
  const string Description() {
    return "prompts for a command (a C string) and runs it";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    switch (editor_state->structure()) {
      case LINE:
        editor_state->ResetStructure();
        RunCppCommandHandler(
            editor_state->current_buffer()->second->current_line()->ToString(),
            editor_state);
        break;

      default:
        Prompt(editor_state, "cpp ", "cpp", "", RunCppCommandHandler,
               EmptyPredictor);
    }
  }
};

static const map<int, Command*>& GetAdvancedModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('q', new Quit()));
    output.insert(make_pair('d', new CloseCurrentBuffer()));
    output.insert(make_pair('w', new SaveCurrentBuffer()));

    vector<string> variables;
    OpenBuffer::BoolStruct()->RegisterVariableNames(&variables);
    OpenBuffer::StringStruct()->RegisterVariableNames(&variables);
    output.insert(make_pair(
        'v',
        NewLinePromptCommand(
            "var ", "variables", "assigns to a variable", SetVariableHandler,
            PrecomputedPredictor(variables)).release()));

    output.insert(make_pair('c', new RunCppCommand()));

    output.insert(make_pair('.', new OpenDirectory()));
    output.insert(make_pair('l', new ListBuffers()));
    output.insert(make_pair('r', new ReloadBuffer()));
    output.insert(make_pair('e', new SendEndOfFile()));
    output.insert(make_pair(
        'o',
        NewLinePromptCommand("<", "files", "loads a file", OpenFileHandler,
                             FilePredictor).release()));
    output.insert(make_pair(
        'F',
        NewLinePromptCommand(
            "...$ ",
            "commands",
            "forks a command for each line in the current buffer",
            RunMultipleCommandsHandler, EmptyPredictor).release()));
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
