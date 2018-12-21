#include <csignal>
#include <iostream>
#include <string>

#include <glog/logging.h>

#include "audio.h"
#include "editor.h"
#include "terminal.h"
#include "tree.h"
#include "wstring.h"

using namespace afc::editor;

unsigned int NextRandom() {
  static const bool read_from_stdin = getenv("EDGE_TEST_STDIN") != nullptr;
  if (!read_from_stdin) {
    return rand();
  }
  char buffer[2];
  std::cin.read(buffer, 2);
  if (std::cin.eof()) {
    exit(0);
  }
  return (static_cast<unsigned int>(buffer[0]) << 8) +
         static_cast<unsigned int>(buffer[1]);
}

void SendInput(EditorState* editor_state, string input) {
  VLOG(5) << "Input: " << input;
  editor_state->ProcessInputString(input);
}

void RandomModifiers(EditorState* editor_state) {
  switch (NextRandom() % 5) {
    case 0:
      break;
    case 1:
      SendInput(editor_state, "w");
      break;
    case 2:
      SendInput(editor_state, "e");
      break;
    case 3:
      SendInput(editor_state, "c");
      break;
    case 4:
      SendInput(editor_state, "P");
      break;
  }
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
  auto audio_player = NewNullAudioPlayer();
  EditorState editor_state(audio_player.get());
  SendInput(&editor_state, "i");
  editor_state.ProcessInput(Terminal::ESCAPE);
  for (int i = 0; i < 1000 || getenv("EDGE_TEST_STDIN") != nullptr; i++) {
    LOG(INFO) << "Iteration: " << i;
    if (NextRandom() % 3 == 0) {
      SendInput(&editor_state, std::to_string(1 + NextRandom() % 5));
    }
    unsigned int value = NextRandom();
    switch (value % 29) {
      case 0:
        SendInput(&editor_state, "h");
        break;

      case 1:
        SendInput(&editor_state, "j");
        break;

      case 2:
        VLOG(5) << "Command: k";
        SendInput(&editor_state, "k");
        break;

      case 3:
        VLOG(5) << "Command: l";
        SendInput(&editor_state, "l");
        break;

      case 4: {
        vector<string> strings = {" ",   "{",   "}",         "(", ")",
                                  "\n+", "\n-", "\n@",       "*", "blah",
                                  "\n",  "a",   "1234567890"};
        auto s = strings[NextRandom() % strings.size()];
        SendInput(&editor_state, "i" + s);
        VLOG(5) << "String was: [" << s << "]";
        editor_state.ProcessInput(Terminal::ESCAPE);
      } break;

      case 5:
        SendInput(&editor_state, "d");
        RandomModifiers(&editor_state);
        SendInput(&editor_state, "\n");
        break;

      case 6:
        SendInput(&editor_state, "u");
        break;

      case 7:
        SendInput(&editor_state, ".");
        break;

      case 8:
        SendInput(&editor_state, "p");
        break;

      case 9:
        SendInput(&editor_state, "+");
        break;

      case 10:
        SendInput(&editor_state, "-");
        break;

      case 11:
        SendInput(&editor_state, "_");
        break;

      case 12:
        SendInput(&editor_state, "=");
        break;

      case 13: {
        int times = NextRandom() % 5;
        VLOG(5) << "Command: i BACKSPACES: " << times;
        SendInput(&editor_state, "i");
        for (int i = 0; i < times; i++) {
          editor_state.ProcessInput(Terminal::BACKSPACE);
        }
        VLOG(5) << "Escape.";
        editor_state.ProcessInput(Terminal::ESCAPE);
      } break;

      case 14:
        SendInput(&editor_state, "g");
        break;

      case 15:
        VLOG(5) << "Command: ~";
        SendInput(&editor_state, "~");
        RandomModifiers(&editor_state);
        SendInput(&editor_state, "\n");
        break;

      case 16:
        SendInput(&editor_state, "/blah.*5");
        break;

      case 17:
        SendInput(&editor_state, "\n");
        break;

      case 18:
        SendInput(&editor_state, "al");
        break;

      case 19:
        SendInput(&editor_state, "b");
        break;

      case 20:
        SendInput(&editor_state, "ar");
        break;

      case 21:
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        SendInput(&editor_state, "afdate\n");
        break;

      case 22:
        editor_state.ProcessInput(Terminal::ESCAPE);
        editor_state.ProcessInput(Terminal::ESCAPE);
        SendInput(&editor_state, "afcat\n");
        break;

      case 23:
        SendInput(&editor_state, "ae\n");
        break;

      case 24:
        SendInput(&editor_state, "fa");
        break;

      case 25:
        SendInput(&editor_state, "f5");
        break;

      case 26:
        SendInput(&editor_state, "vf");
        SendInput(&editor_state, "erg");
        break;

      case 27:
        SendInput(&editor_state, "vp");
        break;

      case 28: {
        vector<string> parsers = {"cpp", "markdown", "diff"};
        auto parser = parsers[NextRandom() % parsers.size()];
        SendInput(&editor_state, "avtree_parser\n" + parser + "\n");
      } break;

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
