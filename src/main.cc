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

#include "config.h"

#include "editor.h"
#include "lazy_string.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "server.h"
#include "terminal.h"
#include "wstring.h"

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

  // Contains C++ (VM) code to execute.
  string commands_to_run;
};

static const char* kDefaultCommandsToRun =
    "OpenFile(\"" DOCDIR "/README\");";

string CommandsToRun(Args args) {
  string commands_to_run = args.commands_to_run;
  for (auto& path : args.files_to_open) {
    commands_to_run += "OpenFile(\"" + string(path) + "\");\n";
  }
  for (auto& command_to_fork : args.commands_to_fork) {
    commands_to_run += "ForkCommand(\"" + string(command_to_fork) + "\");\n";
  }
  if (commands_to_run.empty()) {
    return kDefaultCommandsToRun;
  }
  return commands_to_run;
}

Args ParseArgs(int* argc, const char*** argv) {
  using std::cout;
  using std::cerr;

  string kHelpString = "Usage: edge [OPTION]... [FILE]...\n"
      "Open the files given.\n\nEdge supports the following options:\n"
      "  --fork <shellcmd>: Creates a buffer running a shell command\n"
      "  --help, -h: Displays this message.\n"
      "  --run <vmcmd>: Runs a VM command\n\n"
      "Report bugs to <alefore@gmail.com>\n";

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
    } else if (cmd == "--help" || cmd == "-h") {
      cout << kHelpString;
      exit(0);
    } else if (cmd == "--fork" || cmd == "-f") {
      CHECK(*argc > 1)
          << output.binary_name << ": " << cmd
          << ": Expected command to fork.\n";
      output.commands_to_fork.push_back((*argv)[1]);
      (*argv)++;
      (*argc)--;
    } else if (cmd == "--run") {
      CHECK(*argc > 1)
          << output.binary_name << ": " << cmd
          << ": Expected command to run.\n";
      output.commands_to_run += (*argv)[1];
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

void SendCommandsToParent(int fd, const string commands_to_run) {
  CHECK(fd != -1);
  using std::cerr;
  LOG(INFO) << "Sending commands to parent: " << commands_to_run;
  if (write(fd, commands_to_run.c_str(), commands_to_run.size()) == -1) {
    cerr << "write: " << strerror(errno);
    exit(1);
  }
}

}  // namespace

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::unique_ptr;
  using std::cerr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  string locale = std::setlocale(LC_ALL, "");
  LOG(INFO) << "Using locale: " << locale;

  Args args = ParseArgs(&argc, &argv);

  int fd = MaybeConnectToParentServer();
  if (fd != -1) {
    SendCommandsToParent(fd, CommandsToRun(args));
    cerr << args.binary_name << ": Waiting for EOF ...\n";
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
  auto commands_to_run = CommandsToRun(args);
  if (!commands_to_run.empty()) {
    SendCommandsToParent(MaybeConnectToParentServer(), commands_to_run);
  }

  while (!editor_state()->terminate()) {
    terminal.Display(editor_state());

    std::vector<std::shared_ptr<OpenBuffer>> buffers;

    // The file descriptor at position i will be either fd or fd_error of
    // buffers[i]. The exception to this is fd 0 (at the end).
    struct pollfd fds[editor_state()->buffers()->size() * 2 + 2];
    buffers.reserve(sizeof(fds) / sizeof(fds[0]));

    for (auto& buffer : *editor_state()->buffers()) {
      if (buffer.second->fd() != -1) {
        fds[buffers.size()].fd = buffer.second->fd();
        fds[buffers.size()].events = POLLIN | POLLPRI;
        buffers.push_back(buffer.second);
      }
      if (buffer.second->fd_error() != -1) {
        fds[buffers.size()].fd = buffer.second->fd_error();
        fds[buffers.size()].events = POLLIN | POLLPRI;
        buffers.push_back(buffer.second);
      }
    }

    fds[buffers.size()].fd = 0;
    fds[buffers.size()].events = POLLIN | POLLPRI;

    int results;
    while ((results = poll(fds, buffers.size() + 1, -1)) <= 0) {
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

    for (size_t i = 0; i < buffers.size() + 1; i++) {
      if (!(fds[i].revents & (POLLIN | POLLPRI | POLLHUP))) {
        continue;
      }
      if (fds[i].fd == 0) {
        wint_t c;
        while ((c = terminal.Read(editor_state())) != static_cast<wint_t>(-1)) {
          DCHECK(editor_state()->mode() != nullptr);
          editor_state()->mode()->ProcessInput(c, editor_state());
        }
        continue;
      }

      CHECK_LE(i, buffers.size());
      if (fds[i].fd == buffers[i]->fd()) {
        LOG(INFO) << "Reading (normal): " << buffers[i]->name();
        buffers[i]->ReadData(editor_state());
      } else if (fds[i].fd == buffers[i]->fd_error()) {
        LOG(INFO) << "Reading (error): " << buffers[i]->name();
        buffers[i]->ReadErrorData(editor_state());
      } else {
        LOG(FATAL) << "Invalid file descriptor.";
      }
    }
  }

  delete editor_state();
  terminal.SetStatus(L"done");
}
