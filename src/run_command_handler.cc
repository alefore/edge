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
#include "wstring.h"

namespace {

using namespace afc::editor;
using std::cerr;
using std::to_string;

map<wstring, wstring> LoadEnvironmentVariables(
    const vector<wstring>& path, const wstring& full_command,
    map<wstring, wstring> environment) {
  static const wstring whitespace = L"\t ";
  size_t start = full_command.find_first_not_of(whitespace);
  size_t end = full_command.find_first_of(whitespace, start);
  wstring command = full_command.substr(start, end - start);
  for (auto dir : path) {
    wstring full_path = dir + L"/commands/" + command + L"/environment";
    std::ifstream infile(ToByteString(full_path));
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
      environment.insert(
          make_pair(FromByteString(line.substr(0, equals)),
                    FromByteString(line.substr(equals + 1))));
    }
  }
  return environment;
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(EditorState* editor_state,
                const wstring& name,
                const wstring& command,
                map<wstring, wstring> environment)
      : OpenBuffer(editor_state, name),
        environment_(LoadEnvironmentVariables(
            editor_state->edge_path(),
            command,
            std::move(environment))) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    int pipefd_out[2];
    int pipefd_err[2];
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
      pipefd_out[parent_fd] = master_fd;
      char* pts_path = ptsname(master_fd);
      target->set_string_variable(
          variable_pts_path(), FromByteString(pts_path));
      pipefd_out[child_fd] = open(pts_path, O_RDWR);
      if (pipefd_out[child_fd] == -1) {
        cerr << "open failed: " << pts_path << ": " << string(strerror(errno));
        exit(1);
      }
      pipefd_err[parent_fd] = -1;
      pipefd_err[child_fd] = -1;
    } else if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_out) == -1
               || socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_err) == -1) {
      LOG(FATAL) << "socketpair failed: " << strerror(errno);
      exit(1);
    }

    pid_t child_pid = fork();
    if (child_pid == -1) {
      editor_state->SetStatus(
          L"fork failed: " + FromByteString(strerror(errno)));
      return;
    }
    if (child_pid == 0) {
      close(pipefd_out[parent_fd]);
      close(pipefd_err[parent_fd]);

      if (setsid() == -1) {
        cerr << "setsid failed: " << string(strerror(errno));
        exit(1);
      }

      if (dup2(pipefd_out[child_fd], 0) == -1
          || dup2(pipefd_out[child_fd], 1) == -1
          || dup2(pipefd_err[child_fd] == -1
                      ? pipefd_out[child_fd]
                      : pipefd_err[child_fd],
                  2) == -1) {
        LOG(FATAL) << "dup2 failed!";
      }
      if (pipefd_out[child_fd] != 0
          && pipefd_out[child_fd] != 1
          && pipefd_out[child_fd] != 2) {
        close(pipefd_out[child_fd]);
      }
      if (pipefd_err[child_fd] != 0
          && pipefd_err[child_fd] != 1
          && pipefd_err[child_fd] != 2) {
        close(pipefd_err[child_fd]);
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
        environment.insert(
            make_pair(ToByteString(it.first), ToByteString(it.second)));
      }

      char** envp =
          static_cast<char**>(malloc(sizeof(char*) * (environment.size() + 1)));
      size_t position = 0;
      for (const auto& it : environment) {
        string str = it.first + "=" + it.second;
        assert(position < environment.size());
        envp[position++] = strdup(str.c_str());
      }
      envp[position++] = nullptr;
      CHECK_EQ(position, environment.size() + 1);

      char* argv[] = {
          strdup("sh"),
          strdup("-c"),
          strdup(
              ToByteString(read_string_variable(variable_command())).c_str()),
          nullptr};
      int status = execve("/bin/sh", argv, envp);
      exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }
    close(pipefd_out[child_fd]);
    close(pipefd_err[child_fd]);
    LOG(INFO) << "Setting input files: " << pipefd_out[parent_fd] << ", "
              << pipefd_err[parent_fd];
    target->SetInputFiles(
        editor_state, pipefd_out[parent_fd], pipefd_err[parent_fd],
        read_bool_variable(variable_pts()), child_pid);
    editor_state->ScheduleRedraw();
  }

 private:
  const map<wstring, wstring> environment_;
};

void RunCommand(
    const wstring& name, const wstring& input,
    const map<wstring, wstring> environment, EditorState* editor_state) {
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
  const wstring Description() {
    return L"forks a subprocess";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case CHAR:
        {
          PromptOptions options;
          options.prompt = L"$ ";
          options.history_file = L"commands";
          options.handler = RunCommandHandler;
          Prompt(editor_state, options);
        }
        break;

      case LINE:
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
        editor_state->SetStatus(L"Oops, that structure is not handled.");
    }
    editor_state->ResetStructure();
  }
};

}  // namespace

namespace afc {
namespace editor {

void ForkCommand(EditorState* editor_state, const ForkCommandOptions& options) {
  wstring buffer_name = options.buffer_name.empty()
      ? (L"$ " + options.command)
      : options.buffer_name;
  auto it = editor_state->buffers()->insert(make_pair(buffer_name, nullptr));
  if (it.second) {
    it.first->second.reset(new CommandBuffer(
        editor_state, buffer_name, options.command, options.environment));
    if (editor_state->has_current_buffer()) {
      it.first->second
          ->CopyVariablesFrom(editor_state->current_buffer()->second);
    }
    it.first->second->set_string_variable(
        OpenBuffer::variable_command(), options.command);
    it.first->second->set_string_variable(OpenBuffer::variable_path(), L"");
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

void RunCommandHandler(const wstring& input, EditorState* editor_state) {
  map<wstring, wstring> empty_environment;
  RunCommand(L"$ " + input, input, empty_environment, editor_state);
}

void RunMultipleCommandsHandler(const wstring& input,
                                EditorState* editor_state) {
  if (input.empty() || !editor_state->has_current_buffer()) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }
  auto buffer = editor_state->current_buffer()->second;
  for (const auto& line : *buffer->contents()) {
    wstring arg = line->ToString();
    map<wstring, wstring> environment = {{L"ARG", arg}};
    RunCommand(L"$ " + input + L" " + arg, input, environment, editor_state);
  }
}

}  // namespace editor
}  // namespace afc
