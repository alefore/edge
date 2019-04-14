#include "src/run_command_handler.h"

#include <cstring>
#include <fstream>
#include <iostream>

extern "C" {
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"
#include "src/wstring.h"

namespace {

using namespace afc::editor;
using std::cerr;
using std::to_string;

struct CommandData {
  time_t time_start = 0;
  time_t time_end = 0;
};

map<wstring, wstring> LoadEnvironmentVariables(
    const vector<wstring>& path, const wstring& full_command,
    map<wstring, wstring> environment) {
  static const wstring whitespace = L"\t ";
  size_t start = full_command.find_first_not_of(whitespace);
  if (start == full_command.npos) {
    return environment;
  }
  size_t end = full_command.find_first_of(whitespace, start);
  if (end == full_command.npos || end <= start) {
    return environment;
  }
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
      environment.insert(make_pair(FromByteString(line.substr(0, equals)),
                                   FromByteString(line.substr(equals + 1))));
    }
  }
  return environment;
}

void GenerateContents(EditorState* editor_state,
                      std::map<wstring, wstring> environment, CommandData* data,
                      OpenBuffer* target) {
  int pipefd_out[2];
  int pipefd_err[2];
  static const int parent_fd = 0;
  static const int child_fd = 1;
  time(&data->time_start);
  if (target->Read(buffer_variables::pts())) {
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
    target->Set(buffer_variables::pts_path(), FromByteString(pts_path));
    pipefd_out[child_fd] = open(pts_path, O_RDWR);
    if (pipefd_out[child_fd] == -1) {
      cerr << "open failed: " << pts_path << ": " << string(strerror(errno));
      exit(1);
    }
    pipefd_err[parent_fd] = -1;
    pipefd_err[child_fd] = -1;
  } else if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_out) == -1 ||
             socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_err) == -1) {
    LOG(FATAL) << "socketpair failed: " << strerror(errno);
    exit(1);
  }

  pid_t child_pid = fork();
  if (child_pid == -1) {
    editor_state->SetStatus(L"fork failed: " + FromByteString(strerror(errno)));
    return;
  }
  if (child_pid == 0) {
    LOG(INFO) << "I am the children. Life is beautiful!";

    close(pipefd_out[parent_fd]);
    close(pipefd_err[parent_fd]);

    if (setsid() == -1) {
      cerr << "setsid failed: " << string(strerror(errno));
      exit(1);
    }

    if (dup2(pipefd_out[child_fd], 0) == -1 ||
        dup2(pipefd_out[child_fd], 1) == -1 ||
        dup2(pipefd_err[child_fd] == -1 ? pipefd_out[child_fd]
                                        : pipefd_err[child_fd],
             2) == -1) {
      LOG(FATAL) << "dup2 failed!";
    }
    if (pipefd_out[child_fd] != 0 && pipefd_out[child_fd] != 1 &&
        pipefd_out[child_fd] != 2) {
      close(pipefd_out[child_fd]);
    }
    if (pipefd_err[child_fd] != 0 && pipefd_err[child_fd] != 1 &&
        pipefd_err[child_fd] != 2) {
      close(pipefd_err[child_fd]);
    }

    auto children_path = target->Read(buffer_variables::children_path());
    if (!children_path.empty() &&
        chdir(ToByteString(children_path).c_str()) == -1) {
      LOG(FATAL) << children_path
                 << ": chdir failed: " << string(strerror(errno));
    }

    // Copy variables from the current environment (environ(7)).
    for (size_t index = 0; environ[index] != nullptr; index++) {
      wstring entry = FromByteString(environ[index]);
      size_t eq = entry.find_first_of(L"=");
      if (eq == wstring::npos) {
        environment.insert({entry, L""});
      } else {
        environment.insert({entry.substr(0, eq), entry.substr(eq + 1)});
      }
    }
    environment[L"TERM"] = L"screen";
    LoadEnvironmentVariables(editor_state->edge_path(),
                             target->Read(buffer_variables::command()),
                             environment);

    char** envp =
        static_cast<char**>(calloc(environment.size() + 1, sizeof(char*)));
    size_t position = 0;
    for (const auto& it : environment) {
      string str = ToByteString(it.first) + "=" + ToByteString(it.second);
      CHECK_LT(position, environment.size());
      envp[position++] = strdup(str.c_str());
    }
    envp[position++] = nullptr;
    CHECK_EQ(position, environment.size() + 1);

    char* argv[] = {
        strdup("sh"), strdup("-c"),
        strdup(ToByteString(target->Read(buffer_variables::command())).c_str()),
        nullptr};
    int status = execve("/bin/sh", argv, envp);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
  }
  close(pipefd_out[child_fd]);
  close(pipefd_err[child_fd]);
  LOG(INFO) << "Setting input files: " << pipefd_out[parent_fd] << ", "
            << pipefd_err[parent_fd];
  target->SetInputFiles(editor_state, pipefd_out[parent_fd],
                        pipefd_err[parent_fd],
                        target->Read(buffer_variables::pts()), child_pid);
  editor_state->ScheduleRedraw();
  target->AddEndOfFileObserver([editor_state, data, target]() {
    LOG(INFO) << "End of file notification.";
    int success = WIFEXITED(target->child_exit_status()) &&
                  WEXITSTATUS(target->child_exit_status()) == 0;
    double frequency =
        target->Read(success ? buffer_variables::beep_frequency_success()
                             : buffer_variables::beep_frequency_failure());
    if (frequency > 0.0001) {
      GenerateBeep(editor_state->audio_player(), frequency);
    }
    time(&data->time_end);
  });
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(Options options, std::shared_ptr<CommandData> command_data)
      : OpenBuffer(std::move(options)), data_(std::move(command_data)) {}

  wstring FlagsString() const override {
    wstring initial_information;
    if (child_pid_ != -1) {
      initial_information = L"… ";
    } else if (!WIFEXITED(child_exit_status_)) {
      initial_information = L" ";
    } else if (WEXITSTATUS(child_exit_status_) == 0) {
      initial_information = L" ";
    } else {
      initial_information = L" ";
    }

    wstring additional_information;
    time_t now;
    time(&now);
    if (now > data_->time_start && data_->time_start > 0) {
      time_t end = (child_pid_ != -1 || data_->time_end < data_->time_start)
                       ? now
                       : data_->time_end;
      additional_information +=
          L" run:" + DurationToString(end - data_->time_start);
    }
    if (child_pid_ == -1 && now > data_->time_end) {
      additional_information +=
          L" done:" + DurationToString(now - data_->time_end);
    }
    return initial_information + OpenBuffer::FlagsString() +
           additional_information;
  }

 private:
  static wstring DurationToString(size_t duration) {
    static const std::vector<std::pair<size_t, wstring>> time_units = {
        {60, L"s"}, {60, L"m"}, {24, L"h"}, {99999999, L"d"}};
    size_t factor = 1;
    for (auto& entry : time_units) {
      if (duration < factor * entry.first) {
        return std::to_wstring(duration / factor) + entry.second;
      }
      factor *= entry.first;
    }
    return L"very-long";
  }

  const std::shared_ptr<CommandData> data_;
};

void RunCommand(const wstring& name, const wstring& input,
                map<wstring, wstring> environment, EditorState* editor_state,
                wstring children_path) {
  if (input.empty()) {
    if (editor_state->has_current_buffer()) {
      editor_state->current_buffer()->second->ResetMode();
    }
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }

  ForkCommandOptions options;
  options.command = input;
  options.buffer_name = name;
  options.enter = !editor_state->has_current_buffer() ||
                  !editor_state->current_buffer()->second->Read(
                      buffer_variables::commands_background_mode());
  options.children_path = children_path;
  options.environment = std::move(environment);
  ForkCommand(editor_state, options);
}

void RunCommandHandler(const wstring& input, EditorState* editor_state,
                       size_t i, size_t n, wstring children_path) {
  map<wstring, wstring> environment = {{L"EDGE_RUN", std::to_wstring(i)},
                                       {L"EDGE_RUNS", std::to_wstring(n)}};
  wstring name = children_path + L"$";
  if (n > 1) {
    for (auto& it : environment) {
      name += L" " + it.first + L"=" + it.second;
    }
  }
  name += L" " + input;
  RunCommand(name, input, environment, editor_state, std::move(children_path));
}

wstring GetChildrenPath(EditorState* editor_state) {
  return editor_state->has_current_buffer()
             ? editor_state->current_buffer()->second->Read(
                   buffer_variables::children_path())
             : L"";
}

class ForkEditorCommand : public Command {
 public:
  wstring Description() const override {
    return L"Prompts for a command and creates a new buffer running it.";
  }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->structure() == StructureChar()) {
      PromptOptions options;
      wstring children_path = GetChildrenPath(editor_state);
      options.prompt = children_path + L"$ ";
      options.history_file = L"commands";
      options.handler = [children_path](const wstring& name,
                                        EditorState* editor_state) {
        RunCommandHandler(name, editor_state, 0, 1, children_path);
      };
      Prompt(editor_state, options);
    } else if (editor_state->structure() == StructureLine()) {
      if (!editor_state->has_current_buffer() ||
          editor_state->current_buffer()->second->current_line() == nullptr) {
        return;
      }
      auto children_path = GetChildrenPath(editor_state);
      auto line =
          editor_state->current_buffer()->second->current_line()->ToString();
      for (size_t i = 0; i < editor_state->repetitions(); ++i) {
        RunCommandHandler(line, editor_state, i, editor_state->repetitions(),
                          children_path);
      }
    } else {
      editor_state->SetStatus(L"Oops, that structure is not handled.");
    }
    editor_state->ResetStructure();
  }
};

}  // namespace

namespace afc {
namespace editor {

std::shared_ptr<OpenBuffer> ForkCommand(EditorState* editor_state,
                                        const ForkCommandOptions& options) {
  wstring buffer_name = options.buffer_name.empty() ? (L"$ " + options.command)
                                                    : options.buffer_name;
  auto it = editor_state->buffers()->insert(make_pair(buffer_name, nullptr));
  if (it.second) {
    auto command_data = std::make_shared<CommandData>();
    OpenBuffer::Options buffer_options;
    buffer_options.editor_state = editor_state;
    buffer_options.name = buffer_name;
    buffer_options.generate_contents = [editor_state,
                                        environment = options.environment,
                                        command_data](OpenBuffer* target) {
      GenerateContents(editor_state, environment, command_data.get(), target);
    };
    auto buffer = std::make_shared<CommandBuffer>(std::move(buffer_options),
                                                  command_data);
    buffer->Set(buffer_variables::children_path(), options.children_path);
    buffer->Set(buffer_variables::command(), options.command);
    it.first->second = std::move(buffer);
  } else {
    it.first->second->ResetMode();
  }
  if (options.enter) {
    editor_state->set_current_buffer(it.first);
    editor_state->ScheduleRedraw();
  }
  it.first->second->Reload(editor_state);
  it.first->second->set_current_position_line(0);
  return it.first->second;
}

std::unique_ptr<Command> NewForkCommand() {
  return std::make_unique<ForkEditorCommand>();
}

void RunCommandHandler(const wstring& input, EditorState* editor_state,
                       map<wstring, wstring> environment) {
  RunCommand(L"$ " + input, input, environment, editor_state,
             GetChildrenPath(editor_state));
}

void RunMultipleCommandsHandler(const wstring& input,
                                EditorState* editor_state) {
  if (input.empty() || !editor_state->has_current_buffer()) {
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }
  auto buffer = editor_state->current_buffer()->second;
  buffer->contents()->ForEach([editor_state, input](wstring arg) {
    map<wstring, wstring> environment = {{L"ARG", arg}};
    RunCommand(L"$ " + input + L" " + arg, input, environment, editor_state,
               GetChildrenPath(editor_state));
  });
}

}  // namespace editor
}  // namespace afc
