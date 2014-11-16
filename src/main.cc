#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

extern "C" {
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
}

#include <glog/logging.h>

#include "editor.h"
#include "lazy_string.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "terminal.h"

namespace {

using namespace afc::editor;

EditorState* editor_state() {
  static EditorState* editor_state = new EditorState();
  return editor_state;
}

void SignalHandler(int sig) {
  editor_state()->PushSignal(sig);
}

struct Args {
  string binary_name;
  vector<string> files_to_open;
  vector<string> commands_to_fork;
};

Args ParseArgs(int* argc, const char*** argv) {
  using std::cout;
  using std::cerr;

  string kHelpString = "Edge - A text editor.\n";

  Args output;
  output.binary_name = (*argv)[0];
  (*argv)++;
  (*argc)--;

  while (*argc > 0) {
    string cmd = (*argv)[0];
    if (cmd.empty()) {
      (*argv)++;
      (*argc)--;
      continue;
    }
    if (cmd[0] != '-') {
      output.files_to_open.push_back(cmd);
    } else if (cmd == "--help") {
      cout << kHelpString;
      exit(0);
    } else if (cmd == "--fork") {
      if (*argc == 1) {
        cerr << output.binary_name << ": Expected command to run.\n";
        exit(1);
      }
      output.commands_to_fork.push_back((*argv)[1]);
      (*argv)++;
      (*argc)--;
    } else {
      cerr << output.binary_name << ": Invalid flag: " << cmd << "\n";
      exit(1);
    }
    (*argv)++;
    (*argc)--;
  }

  return output;
}

}  // namespace

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::unique_ptr;
  using std::cerr;

  google::InitGoogleLogging(argv[0]);
  Args args = ParseArgs(&argc, &argv);

  int fd = MaybeConnectToParentServer();
  if (fd != -1) {
    LOG(INFO) << "Connected to parent server.";
    string parent_commands = "";
    for (auto& path : args.files_to_open) {
      parent_commands += "OpenFile(\"" + string(path) + "\");\n";
    }
    for (auto& command : args.commands_to_fork) {
      parent_commands += "ForkCommand(\"" + string(command) + "\");\n";
    }
    if (write(fd, parent_commands.c_str(), parent_commands.size()) == -1) {
      cerr << "write: " << strerror(errno);
      exit(1);
    }
    cerr << argv[0] << ": Waiting for EOF ...\n";
    char buffer[4096];
    while (read(0, buffer, sizeof(buffer)) > 0)
      continue;
    cerr << args.binary_name << ": EOF received, exiting.\n";
    exit(0);
  }

  signal(SIGINT, &SignalHandler);
  signal(SIGTSTP, &SignalHandler);

  Terminal terminal;

  LOG(INFO) << "Starting server.";
  StartServer(editor_state());

  for (auto& path : args.files_to_open) {
    terminal.SetStatus("Loading file...");
    OpenFileOptions options;
    options.editor_state = editor_state();
    options.path = path;
    OpenFile(options);
  }
  for (auto& command : args.commands_to_fork) {
    RunCommandHandler(command, editor_state());
  };

  while (!editor_state()->terminate()) {
    terminal.Display(editor_state());

    vector<shared_ptr<OpenBuffer>> buffers_reading;
    for (auto& buffer : *editor_state()->buffers()) {
      if (buffer.second->fd() == -1) { continue; }
      buffers_reading.push_back(buffer.second);
    }

    struct pollfd fds[buffers_reading.size() + 2];

    for (size_t i = 0; i < buffers_reading.size(); i++) {
      fds[i].fd = buffers_reading[i]->fd();
      fds[i].events = POLLIN | POLLPRI;
    }

    fds[buffers_reading.size()].fd = 0;
    fds[buffers_reading.size()].events = POLLIN | POLLPRI;

    int results;
    while ((results = poll(fds, buffers_reading.size() + 1, -1)) <= 0) {
      if (results == -1) {
        switch (errno) {
          case EINTR:
            editor_state()->ProcessSignals();
            break;
          default:
            cerr << "poll failed, exiting: " << strerror(errno) << "\n";
            exit(-1);
        }
      }
    }

    for (size_t i = 0; i < buffers_reading.size() + 1; i++) {
      if (!(fds[i].revents & (POLLIN | POLLPRI | POLLHUP))) {
        continue;
      }
      if (fds[i].fd == 0) {
        int c;
        while ((c = terminal.Read(editor_state())) != -1) {
          editor_state()->mode()->ProcessInput(c, editor_state());
        }
        continue;
      }

      assert(i < buffers_reading.size());
      buffers_reading[i]->ReadData(editor_state());
    }
  }

  delete editor_state();
  terminal.SetStatus("done");
}
