#include "run_command_handler.h"

#include <fstream>
#include <iostream>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl.h>
}

#include "char_buffer.h"
#include "command_mode.h"
#include "editor.h"
#include "line_prompt_mode.h"

namespace {

using namespace afc::editor;
using std::cerr;
using std::to_string;

void LoadEnvironmentVariables(
    const vector<string>& path, const string& full_command,
    map<string, string>* environment) {
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
      environment->insert(
          make_pair(line.substr(0, equals), line.substr(equals + 1)));
    }
  }
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(EditorState* editor_state,
                const string& name,
                const map<string, string>& environment)
      : OpenBuffer(editor_state, name),
        environment_(environment) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    int pipefd[2];
    static const int parent_fd = 0;
    static const int child_fd = 1;
    if (read_bool_variable(variable_pts())) {
      int master_fd = posix_openpt(O_RDWR);
      if (master_fd == -1) {
        cerr << "posix_openpt failed: " << string(strerror(errno));
        exit(1);
      }
      if (grantpt(master_fd) == -1) {
        cerr << "grantpt failed: " << string(strerror(errno));
        exit(1);
      }
      if (unlockpt(master_fd) == -1) {
        cerr << "unlockpt failed: " << string(strerror(errno));
        exit(1);
      }
      // TODO(alejo): Don't do ioctl here; move that to Terminal, have it set a
      // property in the editor, and read the property here.
      struct winsize screen_size;
      if (ioctl(0, TIOCGWINSZ, &screen_size) == -1) {
        cerr << "ioctl TIOCGWINSZ failed: " << string(strerror(errno));
      }
      screen_size.ws_row--;
      if (ioctl(master_fd, TIOCSWINSZ, &screen_size) == -1) {
        cerr << "ioctl TIOCSWINSZ failed: " << string(strerror(errno));
        exit(1);
      }
      pipefd[parent_fd] = master_fd;
      char* pts_path = ptsname(master_fd);
      target->set_string_variable(variable_pts_path(), pts_path);
      pipefd[child_fd] = open(pts_path, O_RDWR);
      if (pipefd[child_fd] == -1) {
        cerr << "open failed: " << pts_path << ": " << string(strerror(errno));
        exit(1);
      }
    } else if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd) == -1) {
      cerr << "socketpair failed: " << string(strerror(errno));
      exit(1);
    }

    pid_t child_pid = fork();
    if (child_pid == -1) {
      editor_state->SetStatus("fork failed: " + string(strerror(errno)));
      return;
    }
    if (child_pid == 0) {
      close(pipefd[parent_fd]);

      if (setsid() == -1) {
        cerr << "setsid failed: " << string(strerror(errno));
        exit(1);
      }

      if (dup2(pipefd[child_fd], 0) == -1) { exit(1); }
      if (dup2(pipefd[child_fd], 1) == -1) { exit(1); }
      if (dup2(pipefd[child_fd], 2) == -1) { exit(1); }
      if (pipefd[child_fd] != 0
          && pipefd[child_fd] != 1
          && pipefd[child_fd] != 2) {
        close(pipefd[child_fd]);
      }

      map<string, string> environment;

      // Copy variables from the current environment (environ(7)).
      for (size_t index = 0; environ[index] != nullptr; index++) {
        string entry = environ[index];
        size_t eq = entry.find_first_of("=");
        if (eq == string::npos) {
          environment.insert(make_pair(entry, ""));
        } else {
          environment.insert(
              make_pair(entry.substr(0, eq), entry.substr(eq + 1)));
        }
      }
      environment["TERM"] = "screen";

      for (auto it : environment_) {
        environment.insert(it);
      }

      LoadEnvironmentVariables(
          editor_state->edge_path(),
          read_string_variable(variable_command()),
          &environment);

      char** envp =
          static_cast<char**>(malloc(sizeof(char*) * (environment.size() + 1)));
      size_t position = 0;
      for (const auto& it : environment) {
        string str = it.first + "=" + it.second;
        assert(position < environment.size());
        envp[position++] = strdup(str.c_str());
      }
      envp[position++] = nullptr;
      assert(position = environment.size() + 1);

      char* argv[] = {
          strdup("sh"),
          strdup("-c"),
          strdup(read_string_variable(variable_command()).c_str()),
          nullptr};
      int status = execve("/bin/sh", argv, envp);
      exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }
    close(pipefd[child_fd]);
    target->SetInputFile(pipefd[parent_fd], read_bool_variable(variable_pts()),
                         child_pid);
    editor_state->ScheduleRedraw();
  }

 private:
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

  ForkCommandOptions options;
  options.command = input;
  options.buffer_name = name;
  options.enter = !editor_state->has_current_buffer()
      || !editor_state->current_buffer()->second->read_bool_variable(
              OpenBuffer::variable_commands_background_mode());
  options.environment = environment;
  ForkCommand(editor_state, options);

  editor_state->ResetMode();
}

class ForkEditorCommand : public Command {
 public:
  const string Description() {
    return "forks a subprocess";
  }

  void ProcessInput(int, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case EditorState::CHAR:
        Prompt(editor_state, "$ ", "commands", "", RunCommandHandler,
               EmptyPredictor);
        break;

      case EditorState::LINE:
        {
          if (!editor_state->has_current_buffer()
              || editor_state->current_buffer()->second->current_line() == nullptr) {
            return;
          }
          RunCommandHandler(
              editor_state->current_buffer()->second->current_line()
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

void ForkCommand(EditorState* editor_state, const ForkCommandOptions& options) {
  string buffer_name = options.buffer_name.empty()
      ? "$ " + options.command : options.buffer_name;
  auto it = editor_state->buffers()->insert(make_pair(buffer_name, nullptr));
  if (it.second) {
    it.first->second.reset(new CommandBuffer(
        editor_state, buffer_name, options.environment));
    if (editor_state->has_current_buffer()) {
      it.first->second
          ->CopyVariablesFrom(editor_state->current_buffer()->second);
    }
    it.first->second->set_string_variable(
        OpenBuffer::variable_command(), options.command);
    it.first->second->set_string_variable(OpenBuffer::variable_path(), "");
  }
  if (options.enter) {
    editor_state->set_current_buffer(it.first);
    editor_state->ScheduleRedraw();
  }
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
}

unique_ptr<Command> NewForkCommand() {
  return unique_ptr<Command>(new ForkEditorCommand());
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
    string arg = line->ToString();
    map<string, string> environment = {{"ARG", arg}};
    RunCommand("$ " + input + " " + arg, input, environment, editor_state);
  }
}

}  // namespace editor
}  // namespace afc
