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
    int c = terminal.Read();
    editor_state.mode->ProcessInput(c, &editor_state);
  }
  terminal.SetStatus("done");
}
