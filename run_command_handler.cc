#include "search_handler.h"

#include <iostream>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "command_mode.h"
#include "editor.h"

namespace {

using namespace afc::editor;
using std::cerr;

void LoadEnvironmentVariables(
    const vector<string>& path, const string& full_command) {
  static const string whitespace = "\t ";
  size_t start = full_command.find_first_not_of(whitespace);
  size_t end = full_command.find_first_of(whitespace, start);
  string command = full_command.substr(start, end - start);
  for (auto dir : path) {
    string full_path = dir + "/commands/" + command + "/environment";
    int fd = open(full_path.c_str(), O_RDONLY);
    if (fd == -1) { continue; }
    // TODO: This is super lame.  Handle better.  Maybe use PBs?
    char buffer[64 * 1024];
    size_t len = read(fd, buffer, sizeof(buffer) - 1);
    if (len > 0) {
      buffer[len] = 0;
      size_t pos = 0;
      while (pos < len) {
        if (buffer[pos] == '\n') {
          pos++;
          continue;
        }
        auto line_end = strchr(buffer + pos, '\n');
        if (line_end != nullptr) {
          *line_end = 0;
        } else {
          line_end = buffer + len;
        }
        auto equals = strchr(buffer + pos, '=');
        if (equals != nullptr) {
          *equals = 0;
          setenv(buffer + pos, equals + 1, 1);
        }
        pos = line_end - buffer + 1;
      }
    }
    close(fd);
  }
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(const string& command) : command_(command) {}

  void Reload(EditorState* editor_state) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
      exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
      close(0);
      close(pipefd[0]);

      int stdin = open("/dev/null", O_RDONLY);
      if (stdin != 0 && dup2(stdin, 0) == -1) { exit(1); }
      if (dup2(pipefd[1], 1) == -1) { exit(1); }
      if (dup2(pipefd[1], 2) == -1) { exit(1); }
      if (stdin != 0) { close(stdin); }
      if (pipefd[1] != 1 && pipefd[1] != 2) { close(pipefd[1]); }

      LoadEnvironmentVariables(editor_state->edge_path, command_);

      system(command_.c_str());
      exit(0);
    }
    close(pipefd[1]);
    SetInputFile(pipefd[0]);
    // TODO: waitpid(pid, &status_dummy, 0);
    // Both when it's done reading or when it's interrupted.
    editor_state->screen_needs_redraw = true;
  }

 private:
  const string command_;
};

}  // namespace

namespace afc {
namespace editor {

void RunCommandHandler(const string& input, EditorState* editor_state) {
  if (input.empty()) {
    editor_state->mode = NewCommandMode();
    editor_state->status = "";
    editor_state->screen_needs_redraw = true;
    return;
  }

  auto it = editor_state->buffers.insert(make_pair("$ " + input, nullptr));
  editor_state->current_buffer = it.first;
  if (it.second) {
    it.first->second.reset(new CommandBuffer(input));
  }
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  editor_state->screen_needs_redraw = true;
  editor_state->mode = std::move(NewCommandMode());
}

}  // namespace editor
}  // namespace afc
