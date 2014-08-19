#include "run_command_handler.h"

#include <fstream>
#include <iostream>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "command_mode.h"
#include "editor.h"
#include "line_prompt_mode.h"

namespace {

using namespace afc::editor;
using std::to_string;

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
  CommandBuffer(const string& name,
                const string& command,
                const map<string, string>& environment)
      : OpenBuffer(name),
        command_(command),
        environment_(environment) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    int pipefd[2];
    static const int parent_fd = 0;
    static const int child_fd = 1;
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd) == -1) {
      exit(1);
    }

    pid_t child_pid = fork();
    if (child_pid == -1) {
      editor_state->SetStatus("fork failed: " + string(strerror(errno)));
      return;
    }
    if (setpgid(child_pid, 0)) {
      if (child_pid == 0) { exit(1); }
      editor_state->SetStatus("setpgid failed: " + string(strerror(errno)));
      return;
    }
    if (child_pid == 0) {
      close(pipefd[parent_fd]);

      if (dup2(pipefd[child_fd], 0) == -1) { exit(1); }
      if (dup2(pipefd[child_fd], 1) == -1) { exit(1); }
      if (dup2(pipefd[child_fd], 2) == -1) { exit(1); }
      if (pipefd[child_fd] != 0
          && pipefd[child_fd] != 1
          && pipefd[child_fd] != 2) {
        close(pipefd[child_fd]);
      }

      LoadEnvironmentVariables(editor_state->edge_path(), command_);
      for (const auto& it : environment_) {
        setenv(it.first.c_str(), it.second.c_str(), 1);
      }

      // TODO: Don't use system?  Use exec and call the shell directly.
      int status = system(command_.c_str());
      exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }
    close(pipefd[child_fd]);
    target->SetInputFile(pipefd[parent_fd], child_pid);
    editor_state->ScheduleRedraw();
  }

 private:
  const string command_;
  const map<string, string> environment_;
};

void RunCommand(
    const string& name, const string& input,
    const map<string, string> environment, EditorState* editor_state) {
  if (input.empty()) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }

  auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
  if (it.second) {
    it.first->second.reset(new CommandBuffer(name, input, environment));
    if (editor_state->has_current_buffer()) {
      it.first->second->CopyVariablesFrom(
          editor_state->current_buffer()->second);
    }
  }
  editor_state->set_current_buffer(it.first);
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  editor_state->ResetMode();
  editor_state->ScheduleRedraw();
}

class ForkCommand : public Command {
 public:
  const string Description() {
    return "forks a subprocess";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case EditorState::CHAR:
        {
          unique_ptr<Command> command(
              NewLinePromptCommand("$ ", "", RunCommandHandler));
          command->ProcessInput(c, editor_state);
        }
        break;

      case EditorState::LINE:
        {
          if (!editor_state->has_current_buffer()
              || editor_state->current_buffer()->second->contents()->empty()) {
            return;
          }
          RunCommandHandler(
              editor_state->current_buffer()->second->current_line()->contents
                  ->ToString(),
              editor_state);
        }
        break;

      default:
        editor_state->SetStatus("Oops, that structure is not handled.");
    }
    editor_state->ResetStructure();
  }
};

}  // namespace

namespace afc {
namespace editor {

unique_ptr<Command> NewForkCommand() {
  return unique_ptr<Command>(new ForkCommand());
}

void RunCommandHandler(const string& input, EditorState* editor_state) {
  map<string, string> empty_environment;
  RunCommand("$ " + input, input, empty_environment, editor_state);
}

void RunMultipleCommandsHandler(const string& input, EditorState* editor_state) {
  if (input.empty() || !editor_state->has_current_buffer()) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }
  auto buffer = editor_state->current_buffer()->second;
  for (const auto& line : *buffer->contents()) {
    string arg = line->contents->ToString();
    map<string, string> environment = {{"ARG", arg}};
    RunCommand("$ " + input + " " + arg, input, environment, editor_state);
  }
}

}  // namespace editor
}  // namespace afc
