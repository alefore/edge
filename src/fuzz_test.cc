#include <glog/logging.h>

#include <csignal>
#include <iostream>
#include <string>

#include "src/editor.h"
#include "src/infrastructure/audio.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/wstring.h"
#include "src/terminal.h"

using namespace afc::editor;
using afc::infrastructure::ControlChar;
using afc::infrastructure::VectorExtendedChar;
using afc::language::text::LineColumn;

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

void SendInput(EditorState* editor_state, std::wstring input) {
  VLOG(5) << "Input: " << input;
  editor_state->ProcessInput(VectorExtendedChar(input));
}

void RandomModifiers(EditorState* editor_state) {
  switch (NextRandom() % 5) {
    case 0:
      break;
    case 1:
      SendInput(editor_state, L"w");
      break;
    case 2:
      SendInput(editor_state, L"e");
      break;
    case 3:
      SendInput(editor_state, L"c");
      break;
    case 4:
      SendInput(editor_state, L"P");
      break;
  }
}

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
  auto audio_player = afc::infrastructure::audio::NewNullPlayer();
  EditorState editor_state(CommandLineValues(), audio_player.value());
  SendInput(&editor_state, L"i");
  editor_state.ProcessInput({ControlChar::kEscape});
  for (int i = 0; i < 1000 || getenv("EDGE_TEST_STDIN") != nullptr; i++) {
    LOG(INFO) << "Iteration: " << i;
    if (NextRandom() % 3 == 0) {
      SendInput(&editor_state, std::to_wstring(1 + NextRandom() % 5));
    }
    unsigned int value = NextRandom();
    switch (value % 29) {
      case 0:
        SendInput(&editor_state, L"h");
        break;

      case 1:
        SendInput(&editor_state, L"j");
        break;

      case 2:
        VLOG(5) << "Command: k";
        SendInput(&editor_state, L"k");
        break;

      case 3:
        VLOG(5) << "Command: l";
        SendInput(&editor_state, L"l");
        break;

      case 4: {
        std::vector<std::wstring> strings = {
            L" ",   L"{", L"}",    L"(",  L")", L"\n+",       L"\n-",
            L"\n@", L"*", L"blah", L"\n", L"a", L"1234567890"};
        auto s = strings[NextRandom() % strings.size()];
        SendInput(&editor_state, L"i" + s);
        VLOG(5) << "String was: [" << s << "]";
        editor_state.ProcessInput({ControlChar::kEscape});
      } break;

      case 5:
        SendInput(&editor_state, L"d");
        RandomModifiers(&editor_state);
        SendInput(&editor_state, L"\n");
        break;

      case 6:
        SendInput(&editor_state, L"u");
        break;

      case 7:
        SendInput(&editor_state, L".");
        break;

      case 8:
        SendInput(&editor_state, L"p");
        break;

      case 9:
        SendInput(&editor_state, L"+");
        break;

      case 10:
        SendInput(&editor_state, L"-");
        break;

      case 11:
        SendInput(&editor_state, L"_");
        break;

      case 12:
        SendInput(&editor_state, L"=");
        break;

      case 13: {
        int times = NextRandom() % 5;
        VLOG(5) << "Command: i BACKSPACES: " << times;
        SendInput(&editor_state, L"i");
        for (int j = 0; j < times; j++) {
          editor_state.ProcessInput({ControlChar::kBackspace});
        }
        VLOG(5) << "Escape.";
        editor_state.ProcessInput({ControlChar::kEscape});
      } break;

      case 14:
        SendInput(&editor_state, L"g");
        break;

      case 15:
        VLOG(5) << "Command: ~";
        SendInput(&editor_state, L"~");
        RandomModifiers(&editor_state);
        SendInput(&editor_state, L"\n");
        break;

      case 16:
        SendInput(&editor_state, L"/blah.*5");
        break;

      case 17:
        SendInput(&editor_state, L"\n");
        break;

      case 18:
        SendInput(&editor_state, L"al");
        break;

      case 19:
        SendInput(&editor_state, L"b");
        break;

      case 20:
        SendInput(&editor_state, L"ar");
        break;

      case 21:
        editor_state.ProcessInput({ControlChar::kEscape});
        editor_state.ProcessInput({ControlChar::kEscape});
        SendInput(&editor_state, L"afdate\n");
        break;

      case 22:
        editor_state.ProcessInput({ControlChar::kEscape});
        editor_state.ProcessInput({ControlChar::kEscape});
        SendInput(&editor_state, L"afcat\n");
        break;

      case 23:
        SendInput(&editor_state, L"ae\n");
        break;

      case 24:
        SendInput(&editor_state, L"fa");
        break;

      case 25:
        SendInput(&editor_state, L"f5");
        break;

      case 26:
        SendInput(&editor_state, L"vf");
        SendInput(&editor_state, L"erg");
        break;

      case 27:
        SendInput(&editor_state, L"vp");
        break;

      case 28: {
        std::vector<std::wstring> parsers = {L"cpp", L"markdown", L"diff"};
        auto parser = parsers[NextRandom() % parsers.size()];
        SendInput(&editor_state, L"avtree_parser\n" + parser + L"\n");
      } break;

      default:
        CHECK(false) << "Ugh: " << value % 24;
    }
    auto cursors = editor_state.current_buffer()->ptr()->active_cursors();
    if (cursors.size() > 50) {
      std::vector<LineColumn> positions;
      auto it = cursors.begin();
      for (int cursor = 0; cursor < 50; cursor++) {
        positions.push_back(*it);
        ++it;
      }
      editor_state.current_buffer()->ptr()->set_active_cursors({});
      editor_state.current_buffer()->ptr()->set_active_cursors(positions);
    }
  }

  return 0;
}
