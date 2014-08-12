#include <cassert>
#include <cstdio>
#include <cstdlib>
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
#include "line_parser.h"
#include "file_link_mode.h"
#include "terminal.h"
#include "token.h"

int main(int argc, const char* argv[]) {
  using namespace afc::editor;
  using std::unique_ptr;

  Terminal terminal;
  EditorState editor_state;
  for (int i = 1; i < argc; i++) {
    terminal.SetStatus("Loading file...");

    unique_ptr<EditorMode> loader(NewFileLinkMode(argv[i], 0));
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
    fds[buffers_reading.size()].events = POLLIN;


    int results = poll(fds, buffers_reading.size() + 1, -1);
    if (results < 0) {
      exit(-1);
    } else {
      for (size_t i = 0; i < static_cast<size_t>(results); i++) {
        if (!(fds[i].revents & (POLLIN | POLLPRI))) {
          continue;
        }
        if (fds[i].fd == 0) {
          editor_state.mode->ProcessInput(terminal.Read(), &editor_state);
          continue;
        }
        assert(i < buffers_reading.size());
        buffers_reading[i]->ReadData();
      }
    }
  }
  terminal.SetStatus("done");
}
