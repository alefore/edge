#include <cassert>
#include <csignal>
#include <iostream>
#include <string>

#include <glog/logging.h>

#include "editor.h"
#include "tree.h"
#include "terminal.h"
#include "wstring.h"

using namespace afc::editor;

int NextRandom() {
  static const bool read_from_stdin = getenv("EDGE_TEST_STDIN") != nullptr;
  if (!read_from_stdin) {
    return rand();
  }
  char buffer[2];
  std::cin.read(buffer, 2);
  if (std::cin.eof()) { exit(0); }
  return static_cast<int>(buffer[0]) * 256 + static_cast<int>(buffer[1]);
}

std::ostream& operator<<(std::ostream& out, const Node<int>& node);

int main(int, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  google::InitGoogleLogging(argv[0]);
  auto seed = time(NULL);
  if (getenv("EDGE_TEST_SEED") != nullptr) {
    seed = std::stoll(getenv("EDGE_TEST_SEED"));
  }
  LOG(INFO) << "Seed: " << seed;
  std::cout << "Seed: " << seed << std::endl;
  srand(seed);
  EditorState editor_state;
  editor_state.ProcessInputString("i");
  editor_state.ProcessInput(Terminal::ESCAPE);
  for (int i = 0; i < 1000 || getenv("EDGE_TEST_STDIN") != nullptr; i++) {
    LOG(INFO) << "Iteration: " << i;
    switch (NextRandom() % 4) {
      case 0:
        break;
      case 1:
        editor_state.ProcessInputString("w");
        break;
      case 2:
        editor_state.ProcessInputString("e");
        break;
      case 3:
        editor_state.ProcessInputString("c");
        break;
    }
    if (NextRandom() % 3 == 0) {
      editor_state.ProcessInputString(std::to_string(1 + NextRandom() % 5));
    }
    if (NextRandom() % 3 == 0) {
      editor_state.ProcessInputString("r");
    }
    switch (NextRandom() % 24) {
      case 0:
        editor_state.ProcessInputString("h");
        break;

      case 1:
        editor_state.ProcessInputString("j");
        break;

      case 2:
        editor_state.ProcessInputString("k");
        break;

      case 3:
        editor_state.ProcessInputString("l");
        break;

      case 4:
        {
          vector<string> strings = { " ", "blah", "\n", "a", "1234567890" };
          editor_state.ProcessInputString("i");
          editor_state.ProcessInputString(strings[NextRandom() % strings.size()]);
          editor_state.ProcessInput(Terminal::ESCAPE);
        }
        break;

      case 5:
        editor_state.ProcessInputString("d");
        break;

      case 6:
        editor_state.ProcessInputString("u");
        break;

      case 7:
        editor_state.ProcessInputString(".");
        break;

      case 8:
        editor_state.ProcessInputString("p");
        break;

      case 9:
        editor_state.ProcessInputString("+");
        break;

      case 10:
        editor_state.ProcessInputString("-");
        break;

      case 11:
        editor_state.ProcessInputString("_");
        break;

      case 12:
        editor_state.ProcessInputString("=");
        break;

      case 13:
        editor_state.ProcessInputString("i");
        for (int i = NextRandom() % 5; i > 0; --i) {
          editor_state.ProcessInput(Terminal::BACKSPACE);
        }
        editor_state.ProcessInput(Terminal::ESCAPE);
        break;

      case 14:
        editor_state.ProcessInputString("g");
        break;

      case 15:
        editor_state.ProcessInputString("~");
        break;

      case 16:
        {
          editor_state.ProcessInputString("/blah.*5");
          auto cursors = editor_state.current_buffer()->second->active_cursors();
          if (cursors->size() > 50) {
            vector<LineColumn> positions;
            auto it = cursors->begin();
            for (int cursor = 0; cursor < 50; cursor++) {
              positions.push_back(*it);
              ++it;
            }
            editor_state.current_buffer()->second->set_active_cursors(positions);
          }
        }
        break;

      case 17:
        editor_state.ProcessInputString("\n");
        break;

      case 18:
        editor_state.ProcessInputString("al");
        break;

      case 19:
        editor_state.ProcessInputString("b");
        break;

      case 20:
        editor_state.ProcessInputString("ar");
        break;

      case 21:
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInputString("afdate\n");
        break;

      case 22:
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInputString("afcat\n");
        break;

      case 23:
        editor_state.ProcessInputString("ae\n");
        break;
    }
  }

  return 0;
}
