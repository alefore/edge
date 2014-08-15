#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

extern "C" {
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
}

#include "editor.h"
#include "lazy_string.h"
#include "file_link_mode.h"
#include "terminal.h"
#include "token.h"

int main(int argc, const char* argv[]) {
  using namespace afc::editor;
  using std::unique_ptr;
  using std::cerr;
  Terminal terminal;
  EditorState editor_state;
  for (int i = 1; i < argc; i++) {
    terminal.SetStatus("Loading file...");

    unique_ptr<EditorMode> loader(NewFileLinkMode(argv[i], 0, false));
    assert(loader.get() != nullptr);
    loader->ProcessInput('\n', &editor_state);
  }

  while (!editor_state.terminate) {
    terminal.Display(&editor_state);

    vector<shared_ptr<OpenBuffer>> buffers_reading;
    for (auto& buffer : editor_state.buffers) {
      if (buffer.second->fd() == -1) { continue; }
      buffers_reading.push_back(buffer.second);
    }

    struct pollfd fds[buffers_reading.size() + 1];

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
        while ((c = terminal.Read()) != -1) {
          editor_state.mode->ProcessInput(c, &editor_state);
        }
        continue;
      }

      assert(i < buffers_reading.size());
      buffers_reading[i]->ReadData(&editor_state);
    }
  }
  terminal.SetStatus("done");
}
