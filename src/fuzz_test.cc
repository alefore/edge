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

unsigned int NextRandom() {
  static const bool read_from_stdin = getenv("EDGE_TEST_STDIN") != nullptr;
  if (!read_from_stdin) {
    return rand();
  }
  char buffer[2];
  std::cin.read(buffer, 2);
  if (std::cin.eof()) { exit(0); }
  return (static_cast<unsigned int>(buffer[0]) << 8)
      + static_cast<unsigned int>(buffer[1]);
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
        VLOG(5) << "WORD";
        editor_state.ProcessInputString("w");
        break;
      case 2:
        VLOG(5) << "LINE";
        editor_state.ProcessInputString("e");
        break;
      case 3:
        VLOG(5) << "CURSOR";
        editor_state.ProcessInputString("c");
        break;
    }
    if (NextRandom() % 3 == 0) {
      VLOG(5) << "REPETITIONS";
      editor_state.ProcessInputString(std::to_string(1 + NextRandom() % 5));
    }
    if (NextRandom() % 3 == 0) {
      VLOG(5) << "REVERSE";
      editor_state.ProcessInputString("r");
    }
    unsigned int value = NextRandom();
    switch (value % 24) {
      case 0:
        VLOG(5) << "Command: h";
        editor_state.ProcessInputString("h");
        break;

      case 1:
        VLOG(5) << "Command: j";
        editor_state.ProcessInputString("j");
        break;

      case 2:
        VLOG(5) << "Command: k";
        editor_state.ProcessInputString("k");
        break;

      case 3:
        VLOG(5) << "Command: l";
        editor_state.ProcessInputString("l");
        break;

      case 4:
        {
          vector<string> strings = { " ", "blah", "\n", "a", "1234567890" };
          auto s = strings[NextRandom() % strings.size()];
          VLOG(5) << "Command: insert: " << s;
          editor_state.ProcessInputString("i");
          editor_state.ProcessInputString(s);
          editor_state.ProcessInput(Terminal::ESCAPE);
        }
        break;

      case 5:
        VLOG(5) << "Command: d";
        editor_state.ProcessInputString("d");
        break;

      case 6:
        VLOG(5) << "Command: u";
        editor_state.ProcessInputString("u");
        break;

      case 7:
        VLOG(5) << "Command: .";
        editor_state.ProcessInputString(".");
        break;

      case 8:
        VLOG(5) << "Command: p";
        editor_state.ProcessInputString("p");
        break;

      case 9:
        VLOG(5) << "Command: +";
        editor_state.ProcessInputString("+");
        break;

      case 10:
        VLOG(5) << "Command: -";
        editor_state.ProcessInputString("-");
        break;

      case 11:
        VLOG(5) << "Command: _";
        editor_state.ProcessInputString("_");
        break;

      case 12:
        VLOG(5) << "Command: =";
        editor_state.ProcessInputString("=");
        break;

      case 13:
        VLOG(5) << "Command: i BACKSPACES";
        editor_state.ProcessInputString("i");
        for (int i = NextRandom() % 5; i > 0; --i) {
          editor_state.ProcessInput(Terminal::BACKSPACE);
        }
        editor_state.ProcessInput(Terminal::ESCAPE);
        break;

      case 14:
        VLOG(5) << "Command: g";
        editor_state.ProcessInputString("g");
        break;

      case 15:
        VLOG(5) << "Command: ~";
        editor_state.ProcessInputString("~");
        break;

      case 16:
        VLOG(5) << "Command: /blah.*5";
        editor_state.ProcessInputString("/blah.*5");
        break;

      case 17:
        VLOG(5) << "Command: \\n";
        editor_state.ProcessInputString("\n");
        break;

      case 18:
        VLOG(5) << "Command: al";
        editor_state.ProcessInputString("al");
        break;

      case 19:
        VLOG(5) << "Command: b";
        editor_state.ProcessInputString("b");
        break;

      case 20:
        VLOG(5) << "Command: ar";
        editor_state.ProcessInputString("ar");
        break;

      case 21:
        VLOG(5) << "Command: afdate";
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInputString("afdate\n");
        break;

      case 22:
        VLOG(5) << "Command: afcat";
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInputString("afcat\n");
        break;

      case 23:
        VLOG(5) << "Command: ae";
        editor_state.ProcessInputString("ae\n");
        break;

      default:
        CHECK(false) << "Ugh: " << value % 24;
    }
    auto cursors = editor_state.current_buffer()->second->active_cursors();
    if (cursors->size() > 50) {
      vector<LineColumn> positions;
      auto it = cursors->begin();
      for (int cursor = 0; cursor < 50; cursor++) {
        positions.push_back(*it);
        ++it;
      }
      editor_state.current_buffer()->second->set_active_cursors({});
      editor_state.current_buffer()->second->set_active_cursors(positions);
    }
  }

  return 0;
}
