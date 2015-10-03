#include <cerrno>
#include <cstring>
#include <locale>
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
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

wstring TrimWhitespace(const wstring& in) {
  size_t begin = in.find_first_not_of(' ', 0);
  if (begin == string::npos) {
    return L"";
  }
  size_t end = in.find_last_not_of(' ', in.size());
  if (end == string::npos) {
    return L"";
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
  const wstring Description() {
    return L"quits";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    wstring error_description;
    if (!editor_state->AttemptTermination(&error_description)) {
      editor_state->SetStatus(error_description);
    }
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

class OpenDirectory : public Command {
  const wstring Description() {
    return L"opens a view of the current directory";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    wstring path;
    if (!editor_state->has_current_buffer()) {
      path = L".";
    } else {
      // TODO: We could alter ToByteString to return a char* and avoid the extra
      // copy.
      char* tmp = strdup(
          ToByteString(editor_state->current_buffer()->first.c_str()).c_str());
      path = FromByteString(dirname(tmp));
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
  const wstring Description() {
    return L"closes the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
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
  const wstring Description() {
    return L"saves the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
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

void OpenFileHandler(const wstring& name, EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.path = name;
  OpenFile(options);
  editor_state->ResetMode();
}

void SetVariableHandler(const wstring& input_name, EditorState* editor_state) {
  editor_state->ResetMode();
  editor_state->ScheduleRedraw();
  wstring name = TrimWhitespace(input_name);
  if (name.empty()) { return; }
  {
    auto var = OpenBuffer::StringStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      Prompt(
          editor_state,
          name + L" := ",
          L"values",
          editor_state->current_buffer()->second->read_string_variable(var),
          [var](const wstring& input, EditorState* editor_state) {
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
          name + L" := " + (buffer->read_bool_variable(var) ? L"ON" : L"OFF"));
      return;
    }
  }
  {
    auto var = OpenBuffer::IntStruct()->find_variable(name);
    if (var != nullptr) {
      if (!editor_state->has_current_buffer()) { return; }
      auto buffer = editor_state->current_buffer()->second;
      Prompt(
          editor_state,
          name + L" := ",
          L"values",
          std::to_wstring(
              editor_state->current_buffer()->second->read_int_variable(var)),
          [var](const wstring& input, EditorState* editor_state) {
            if (!editor_state->has_current_buffer()) { return; }
            try {
              editor_state->current_buffer()
                  ->second->set_int_variable(var, stoi(input));
            } catch (const std::invalid_argument& ia) {
              editor_state->SetStatus(
                  L"Invalid value for integer value “" + var->name() + L"”: " +
                  FromByteString(ia.what()));
            }
            // ResetMode causes the prompt to be deleted, and the captures of
            // this lambda go away with it.
            editor_state->ResetMode();
          },
          &EmptyPredictor);
      return;
    }
  }
  editor_state->SetStatus(L"Unknown variable: " + name);
}

class ActivateBufferLineCommand : public EditorMode {
 public:
  ActivateBufferLineCommand(const wstring& name) : name_(name) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
      case '\n':  // Open the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus(L"Buffer not found: " + name_);
            return;
          }
          editor_state->ResetStatus();
          editor_state->set_current_buffer(it);
          it->second->Enter(editor_state);
          editor_state->PushCurrentPosition();
          editor_state->ScheduleRedraw();
          editor_state->ResetMode();
          break;
        }
      case 'd':  // Delete (close) the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->CloseBuffer(it);
          break;
        }
      case 'r':  // Reload the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->SetStatus(L"Reloading buffer: " + name_);
          it->second->Reload(editor_state);
          break;
        }
    }
  }

 private:
  const wstring name_;
};

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(EditorState* editor_state, const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_atomic_lines(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents();
    AppendLine(editor_state, std::move(NewCopyString(L"Open Buffers:")));
    for (const auto& it : *editor_state->buffers()) {
      wstring flags = it.second->FlagsString();
      auto name =
          NewCopyString(it.first + (flags.empty() ? L"" : L"  ") + flags);
      target->AppendLine(editor_state, std::move(name));
      (*target->contents()->rbegin())->set_activate(
          unique_ptr<EditorMode>(new ActivateBufferLineCommand(it.first)));
    }
    editor_state->ScheduleRedraw();
  }
};

class ListBuffers : public Command {
 public:
  const wstring Description() {
    return L"lists all open buffers";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
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
  const wstring Description() {
    return L"reloads the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case LINE:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          if (buffer->current_line() != nullptr &&
              buffer->current_line()->activate() != nullptr) {
            buffer->current_line()->activate()->ProcessInput('r', editor_state);
          }
        }
        break;
      default:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer();
          buffer->second->Reload(editor_state);
        }
    }
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
  }
};

class SendEndOfFile : public Command {
 public:
  const wstring Description() {
    return L"stops writing to a subprocess (effectively sending EOF).";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ResetMode();
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    if (buffer->fd() == -1) {
      editor_state->SetStatus(L"No active subprocess for current buffer.");
      return;
    }
    if (buffer->read_bool_variable(OpenBuffer::variable_pts())) {
      char str[1] = { 4 };
      if (write(buffer->fd(), str, sizeof(str)) == -1) {
        editor_state->SetStatus(
            L"Sending EOF failed: " + FromByteString(strerror(errno)));
        return;
      }
      editor_state->SetStatus(L"EOF sent");
    } else {
      if (shutdown(buffer->fd(), SHUT_WR) == -1) {
        editor_state->SetStatus(
            L"shutdown(SHUT_WR) failed: " + FromByteString(strerror(errno)));
        return;
      }
      editor_state->SetStatus(L"shutdown sent");
    }
  }
};

void RunCppCommandHandler(const wstring& name, EditorState* editor_state) {
  if (!editor_state->has_current_buffer()) { return; }
  editor_state->ResetMode();
  editor_state->current_buffer()->second->EvaluateString(editor_state, name);
}

class RunCppCommand : public Command {
 public:
  const wstring Description() {
    return L"prompts for a command (a C string) and runs it";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    switch (editor_state->structure()) {
      case LINE:
        editor_state->ResetStructure();
        RunCppCommandHandler(
            editor_state->current_buffer()->second->current_line()->ToString(),
            editor_state);
        break;

      default:
        Prompt(editor_state, L"cpp ", L"cpp", L"", RunCppCommandHandler,
               EmptyPredictor);
    }
  }
};

static const map<wchar_t, Command*>& GetAdvancedModeMap() {
  static map<wchar_t, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('q', new Quit()));
    output.insert(make_pair('d', new CloseCurrentBuffer()));
    output.insert(make_pair('w', new SaveCurrentBuffer()));

    vector<wstring> variables;
    OpenBuffer::BoolStruct()->RegisterVariableNames(&variables);
    OpenBuffer::StringStruct()->RegisterVariableNames(&variables);
    OpenBuffer::IntStruct()->RegisterVariableNames(&variables);
    output.insert(make_pair(
        'v',
        NewLinePromptCommand(
            L"var ", L"variables", L"assigns to a variable", SetVariableHandler,
            PrecomputedPredictor(variables, '_')).release()));

    output.insert(make_pair('c', new RunCppCommand()));

    output.insert(make_pair('.', new OpenDirectory()));
    output.insert(make_pair('l', new ListBuffers()));
    output.insert(make_pair('r', new ReloadBuffer()));
    output.insert(make_pair('e', new SendEndOfFile()));
    output.insert(make_pair(
        'o',
        NewLinePromptCommand(L"<", L"files", L"loads a file", OpenFileHandler,
                             FilePredictor).release()));
    output.insert(make_pair(
        'F',
        NewLinePromptCommand(
            L"...$ ",
            L"commands",
            L"forks a command for each line in the current buffer",
            RunMultipleCommandsHandler, EmptyPredictor).release()));
    output.insert(make_pair('f', NewForkCommand().release()));
    output.insert(make_pair(
        '?',
        NewHelpCommand(output, L"advance command mode").release()));
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
