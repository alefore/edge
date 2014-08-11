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
    unique_ptr<EditorMode> loader(NewFileLinkMode(path, 0));
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
    if (!buffer->saveable) {
      editor_state->status = "Buffer can't be saved.";
      return;
    }
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

class OpenBufferCommand : public EditorMode {
 public:
  OpenBufferCommand(const string& name) : name_(name) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto it = editor_state->buffers.find(name_);
    if (it == editor_state->buffers.end()) {
      // TODO: Keep a function and re-open the buffer?
      editor_state->status = "Buffer not found: " + name_;
      return;
    }
    editor_state->current_buffer = it;
    editor_state->screen_needs_redraw = true;
    editor_state->status = "";
    editor_state-> mode = std::move(NewCommandMode());
  }

 private:
  const string name_;
};

class ListBuffers : public Command {
 public:
  const string Description() {
    return "lists all open buffers";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    auto it = editor_state->buffers.insert(make_pair("open buffers", nullptr));
    editor_state->current_buffer = it.first;
    it.first->second.reset(Load(editor_state).release());

    editor_state->screen_needs_redraw = true;
    editor_state->status = "";
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }

 private:
  unique_ptr<OpenBuffer> Load(EditorState* editor_state) {
    unique_ptr<OpenBuffer> buffer(new OpenBuffer());
    {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyString("Open Buffers:").release());
      buffer->contents.push_back(std::move(line));
    }
    for (const auto& it : editor_state->buffers) {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyString(it.first).release());
      line->activate.reset(new OpenBufferCommand(it.first));
      buffer->contents.push_back(std::move(line));
    }
    return std::move(buffer);
  }
};

class ReloadBuffer : public Command {
 public:
  const string Description() {
    return "reloads the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->current_buffer == editor_state->buffers.end()) {
      return;
    }
    shared_ptr<OpenBuffer> buffer(editor_state->get_current_buffer());
    buffer->contents.clear();
    buffer->loader(editor_state->get_current_buffer().get());
    editor_state->screen_needs_redraw = true;
    editor_state->status = "";
    editor_state->mode = std::move(NewCommandMode());
  }
};

static const map<int, Command*>& GetAdvancedModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('k', new CloseCurrentBuffer()));
    output.insert(make_pair('w', new SaveCurrentBuffer()));
    output.insert(make_pair('.', new OpenDirectory()));
    output.insert(make_pair('l', new ListBuffers()));
    output.insert(make_pair('r', new ReloadBuffer()));
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
