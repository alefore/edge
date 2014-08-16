#include "search_handler.h"

#include <fstream>
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
    std::ifstream infile(full_path);
    if (!infile.is_open()) {
      continue;
    }
    std::string line;
    while (std::getline(infile, line)) {
      if (line == "") {
        continue;
      }
      size_t equals = line.find('=');
      if (equals == line.npos) {
        continue;
      }
      setenv(line.substr(0, equals).c_str(), line.substr(equals + 1).c_str(), 1);
    }
  }
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(const string& command, const map<string, string>& environment)
      : command_(command), environment_(environment) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
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
      for (const auto& it : environment_) {
        setenv(it.first.c_str(), it.second.c_str(), 1);
      }

      system(command_.c_str());
      exit(0);
    }
    close(pipefd[1]);
    target->SetInputFile(pipefd[0]);
    // TODO: waitpid(pid, &status_dummy, 0);
    // Both when it's done reading or when it's interrupted.
    editor_state->screen_needs_redraw = true;
  }

 private:
  const string command_;
  const map<string, string> environment_;
};

void RunCommand(
    const string& name, const string& input,
    const map<string, string> environment, EditorState* editor_state) {
  if (input.empty()) {
    editor_state->mode = NewCommandMode();
    editor_state->status = "";
    editor_state->screen_needs_redraw = true;
    return;
  }

  auto it = editor_state->buffers.insert(make_pair(name, nullptr));
  editor_state->current_buffer = it.first;
  if (it.second) {
    it.first->second.reset(new CommandBuffer(input, environment));
  }
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  editor_state->screen_needs_redraw = true;
  editor_state->mode = std::move(NewCommandMode());
}

}  // namespace

namespace afc {
namespace editor {

void RunCommandHandler(const string& input, EditorState* editor_state) {
  map<string, string> empty_environment;
  RunCommand("$ " + input, input, empty_environment, editor_state);
}

void RunMultipleCommandsHandler(const string& input, EditorState* editor_state) {
  if (input.empty()
      || editor_state->current_buffer == editor_state->buffers.end()) {
    editor_state->mode = NewCommandMode();
    editor_state->status = "";
    editor_state->screen_needs_redraw = true;
    return;
  }
  auto buffer = editor_state->get_current_buffer();
  for (const auto& line : *buffer->contents()) {
    string arg = line->contents->ToString();
    map<string, string> environment = {{"ARG", arg}};
    RunCommand("$ " + input + " " + arg, input, environment, editor_state);
  }
}

}  // namespace editor
}  // namespace afc
