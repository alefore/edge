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

}  // namespace

int main(int argc, const char* argv[]) {
  using namespace afc::editor;
  using std::unique_ptr;
  using std::cerr;

  google::InitGoogleLogging(argv[0]);
  int fd = MaybeConnectToParentServer();
  if (fd != -1) {
    LOG(INFO) << "Connected to parent server.";
    for (int i = 1; i < argc; i++) {
      string command = "OpenFile(\"" + string(argv[i]) + "\");\n";
      if (write(fd, command.c_str(), command.size()) == -1) {
        cerr << "write: " << strerror(errno);
        exit(1);
      }
    }

    cerr << argv[0] << ": Waiting for EOF ...\n";
    char buffer[4096];
    while (read(0, buffer, sizeof(buffer)) > 0)
      continue;
    cerr << argv[0] << ": EOF received, exiting.\n";
    exit(0);
  }

  signal(SIGINT, &SignalHandler);
  signal(SIGTSTP, &SignalHandler);

  Terminal terminal;

  LOG(INFO) << "Starting server.";
  StartServer(editor_state());

  for (int i = 1; i < argc; i++) {
    terminal.SetStatus("Loading file...");
    OpenFileOptions options;
    options.editor_state = editor_state();
    options.path = argv[i];
    OpenFile(options);
  }

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
  terminal.SetStatus("done");
}
