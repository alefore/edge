#include <csignal>
#include <iostream>
#include <string>

#include <glog/logging.h>
#include "wstring.h"

#include "audio.h"
#include "buffer_variables.h"
#include "editor.h"
#include "src/test/buffer_contents_test.h"
#include "src/test/line_test.h"
#include "terminal.h"
#include "tree.h"

using namespace afc::editor;

void CheckIsEmpty(EditorState* editor_state) {
  CHECK_EQ(editor_state->current_buffer()->second->contents()->size(), 1);
  CHECK_EQ(editor_state->current_buffer()->second->contents()->at(0)->size(),
           0);
}

void Clear(EditorState* editor_state) {
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->set_current_buffer(
      editor_state->buffers()->find(L"anonymous buffer 0"));

  editor_state->ProcessInputString("eegdl999999999999999\n");
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->current_buffer()->second->Set(
      buffer_variables::multiple_cursors(), false);
  editor_state->current_buffer()->second->DestroyOtherCursors();
  editor_state->current_buffer()->second->set_position(LineColumn());
  CheckIsEmpty(editor_state);
}

void ShowList(list<int> l) {
  std::cout << "List:";
  for (auto i : l) {
    std::cout << " " << i;
  }
  std::cout << "\n";
}

void TreeTestsLong() {
  srand(0);
  list<int> l;
  Tree<int> t;
  size_t elements = 500;
  for (size_t i = 0; i < elements; i++) {
    int position = rand() % (1 + t.size());
    auto l_it = l.begin();
    auto t_it = t.begin();
    for (int i = 0; i < position; i++) {
      CHECK(t_it == t.begin() + i);
      ++l_it;
      ++t_it;
    }
    CHECK(t_it == t.begin() + position);
    if (position > 10) {
      t_it = t.begin();
      t_it += position / 2;
      t_it += position - position / 2;
      CHECK(t_it == t.begin() + position);
    }
    l.insert(l_it, i);
    t.insert(t.begin() + position, i);
  }
  CHECK(list<int>(t.begin(), t.end()) == l);
  LOG(INFO) << "Starting delete tests.";
  for (size_t i = 0; i < elements / 2; i++) {
    int position = rand() % t.size();
    auto l_it = l.begin();
    auto t_it = t.begin();
    for (int i = 0; i < position; i++) {
      CHECK(t_it == t.begin() + i);
      ++l_it;
      ++t_it;
    }
    CHECK_EQ(*l_it, *t_it);
    CHECK(t_it == t.begin() + position);
    LOG(INFO) << "Erasing at position " << (position) << ": ";
    l.erase(l_it);
    t.erase(t.begin() + position);
    CHECK_EQ(t.size(), l.size());
    CHECK(list<int>(t.begin(), t.end()) == l);
  }

  LOG(INFO) << "Starting sorting tests.";

  vector<int> v(t.begin(), t.end());
  std::sort(v.begin(), v.end());
  std::sort(t.begin(), t.end());
  CHECK_EQ(t.size(), v.size());
  CHECK(vector<int>(t.begin(), t.end()) == v);
}

std::ostream& operator<<(std::ostream& out, const Node<int>& node);

void TreeTestsBasic() {
  Tree<int> t;
  std::cout << t << "\n";
  CHECK(t.begin() == t.end());

  t.push_back(10);
  std::cout << t << "\n";
  CHECK_EQ(t.front(), 10);
  CHECK_EQ(t.back(), 10);
  CHECK(t.begin() != t.end());

  t.push_back(20);
  std::cout << t << "\n";
  CHECK_EQ(t.front(), 10);
  CHECK_EQ(t.back(), 20);

  {
    auto it = t.begin();
    CHECK_EQ(*it, 10);
    ++it;
    CHECK_EQ(*it, 20);
    ++it;
    CHECK(it == t.end());
  }

  t.push_back(30);
  std::cout << t << "\n";
  CHECK_EQ(t.front(), 10);
  CHECK_EQ(t.back(), 30);

  t.push_back(40);
  std::cout << t << "\n";
  CHECK_EQ(t.front(), 10);
  CHECK_EQ(t.back(), 40);

  t.insert(t.begin(), 5);
  std::cout << t << "\n";
  CHECK_EQ(t.front(), 5);

  {
    auto it = t.begin();
    CHECK(it == t.begin());
    CHECK_EQ(*it, 5);
    ++it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 10);
    ++it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 20);
    ++it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 30);
    --it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 20);
    ++it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 30);
    ++it;
    CHECK(it != t.end());
    CHECK_EQ(*it, 40);
    ++it;
    CHECK(it == t.end());
  }

  {
    auto it = t.begin();
    it += 3;
    CHECK_EQ(*it, 30);
  }
}

void TestCases() {
  auto audio_player = NewNullAudioPlayer();
  EditorState editor_state(command_line_arguments::Values(),
                           audio_player.get());
  CHECK(!editor_state.has_current_buffer());

  editor_state.ProcessInputString("i\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("ib");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "\nb");
  editor_state.ProcessInputString(".u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "\nb");

  // Caused a crash (found by fuzz testing).
  editor_state.ProcessInputString("5i\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("+");
  editor_state.ProcessInputString("3k");
  editor_state.ProcessInputString("iblah");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("+_");
  editor_state.ProcessInputString("j.");
  editor_state.ProcessInputString("u");
  editor_state.ProcessInputString("i");
  editor_state.ProcessInput(Terminal::BACKSPACE);
  editor_state.ProcessInput(Terminal::ESCAPE);

  Clear(&editor_state);

  editor_state.ProcessInputString("i");
  CHECK(editor_state.has_current_buffer());
  editor_state.ProcessInputString("alejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("i forero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK(editor_state.current_buffer()->second->current_line()->ToString() ==
        L"alejo forero");
  editor_state.ProcessInputString("gde\n");
  CHECK(editor_state.current_buffer()->second->ToString().empty());

  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 3);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 2);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_col(),
           sizeof("cuervo") - 1);
  editor_state.ProcessInputString("ehhh");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 1);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_col(),
           sizeof("cuervo") - 1 - 2);

  editor_state.ProcessInputString("k");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);
  editor_state.ProcessInputString("kkkkk");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);

  editor_state.ProcessInputString("3g");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_col(),
           3 - 1);

  editor_state.ProcessInputString("rg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_col(),
           sizeof("alejandro") - 1);

  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 2);

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);
  CHECK_EQ(editor_state.current_buffer()->second->current_position_col(), 0);

  editor_state.ProcessInputString("d2el\n");
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 1);
  CHECK_EQ(
      ToByteString(
          editor_state.current_buffer()->second->current_line()->ToString()),
      "cuervo");

  editor_state.ProcessInputString("pp");
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 5);

  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 4);
  editor_state.ProcessInputString("eg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);

  editor_state.ProcessInputString("eel");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 1);

  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("d3\n");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 1);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  Clear(&editor_state);

  editor_state.ProcessInputString("ialejandro forero cuervo\n\n");
  editor_state.ProcessInputString("0123456789abcdefghijklmnopqrstuvwxyz");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("2h2h2h2h2l2l2l2l2l2h2h2h2hegg");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 0);

  editor_state.ProcessInputString("2l2l2l2l2l");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 10);

  editor_state.ProcessInputString("3b");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 4);

  editor_state.ProcessInputString("2rb");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 8);

  editor_state.ProcessInputString("eb");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 2);

  editor_state.ProcessInputString("gf1f3f5f7f9");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 9);

  editor_state.ProcessInputString("b");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 7);

  editor_state.ProcessInputString("10g");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 9);

  editor_state.ProcessInputString("/123\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 2);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 1);

  editor_state.ProcessInputString("egd1000000000000000000\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);

  editor_state.ProcessInputString("ialejo forero\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "kg"
      "d3\n"
      "rg"
      "jp");
  editor_state.ProcessInputString(
      "krg"
      "j"
      "rfa");

  Clear(&editor_state);

  editor_state.ProcessInputString("ihey there hey hey man yes ahoheyblah.");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("gw/");
  CHECK(editor_state.last_search_query() == L"hey");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 10);

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("jjjj");
  editor_state.ProcessInputString("/alejo\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 0);

  Clear(&editor_state);

  // VM Tests.
  editor_state.ProcessInputString("i0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 10);

  editor_state.ProcessInputString("aCSetPositionColumn(4);;\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 4);
  editor_state.ProcessInputString("aCSetPositionColumn(4 - 1);;\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 3);
  editor_state.ProcessInputString("aCSetPositionColumn(8 - 2 * 3 + 5);;\n");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 7);

  Clear(&editor_state);

  // Test for undo after normal delete line.
  editor_state.ProcessInputString("i12345\n67890");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "12345\n67890");

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 0));

  editor_state.ProcessInputString("de5\n");
  CheckIsEmpty(&editor_state);

  editor_state.ProcessInput('u');
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "12345\n67890");

  Clear(&editor_state);

  // Test for insertion at EOF.
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 1);
  editor_state.ProcessInputString("55ji\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 2);

  Clear(&editor_state);

  // Test for uppercase switch
  editor_state.ProcessInputString("ialeJAnDRo\nfoRero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kg~5\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "ALEjanDRo\nfoRero");
  editor_state.ProcessInputString("~w\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "ALEjaNdrO\nfoRero");

  Clear(&editor_state);

  // Test that delete word across multiple lines works.
  editor_state.ProcessInputString("ialejandro\n\n\n\n  forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro\n\n\n\n  forero cuervo");

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 0));

  editor_state.ProcessInputString("rg");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 9));

  editor_state.ProcessInputString("dwk\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandroforero cuervo");

  Clear(&editor_state);

  // Test multiple cursors.
  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro\nforero\ncuervo");

  editor_state.ProcessInputString("g");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(2));

  editor_state.ProcessInputString("+eg");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0));

  editor_state.ProcessInputString("w+");
  editor_state.ProcessInputString("_");
  CHECK(editor_state.current_buffer()->second->Read(
      buffer_variables::multiple_cursors()));

  editor_state.ProcessInputString("i1234 ");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "1234 alejandro\n1234 forero\n1234 cuervo");
  Clear(&editor_state);

  // Test multiple cursors in same line, movement.
  editor_state.ProcessInputString("ialejandro forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro forero cuervo");
  editor_state.ProcessInputString("rfc+gw+");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("avmultiple_cursors\n");
  editor_state.ProcessInputString("ll");
  editor_state.ProcessInputString("i[");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "al[ejandro fo[rero cu[ervo");

  editor_state.ProcessInputString(
      "d\n"
      "l"
      "dr\n"
      "l");
  editor_state.ProcessInputString("i)");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "al[a)ndro fo[r)o cu[v)o");

  Clear(&editor_state);

  editor_state.ProcessInputString("i123\n56\n789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "h"
      "+");                                // Leave a cursor at 9.
  editor_state.ProcessInputString("khh");  // Cursor at 5.
  editor_state.ProcessInputString("i4");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "123\n456\n789");
  editor_state.ProcessInputString("+");    // Leave a cursor at 5.
  editor_state.ProcessInputString("kll");  // Leave cursor at end of first line.
  // Bugs happen here! Did the cursors get adjusted?
  editor_state.ProcessInputString("d\n");
  editor_state.ProcessInputString(
      "_"
      "ix");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "123x4x56\n78x9");

  Clear(&editor_state);

  editor_state.ProcessInputString("ioo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "/o\n"
      "cl"
      "-");

  Clear(&editor_state);

  editor_state.ProcessInputString("i\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "k"
      "~");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "\n");

  Clear(&editor_state);

  editor_state.ProcessInputString("i\n-");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k~");

  Clear(&editor_state);

  // Can cancel the search prompt.
  editor_state.ProcessInputString("/");
  editor_state.ProcessInput(Terminal::ESCAPE);

  Clear(&editor_state);

  // Search switching cursors.
  editor_state.ProcessInputString("i0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("g");
  editor_state.ProcessInputString("+");  // Cursors: 0, *0
  editor_state.ProcessInputString(
      "2l"
      "+");                               // Cursors: 0, 2, *2
  editor_state.ProcessInputString("2l");  // Cursors: 0, 2, *4
  editor_state.ProcessInputString("ch");  // Cursors: 0, *2, 4
  editor_state.ProcessInputString("i-");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "01-23456789");

  Clear(&editor_state);

  // Behavior with moving past end of line.
  editor_state.ProcessInputString("i0123\n0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k3h");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 1));

  Clear(&editor_state);

  editor_state.ProcessInputString("i01\n23\n45\n67\n89\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("3k");  // Cursor at line "45".
  editor_state.ProcessInputString("del\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "01\n23\n67\n89\n");
  editor_state.ProcessInputString(".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "01\n23\n89\n");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("h");
  editor_state.ProcessInputString("dwkhh\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "a");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia\nbcd");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "k"
      "dwk\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "abcd");

  Clear(&editor_state);

  // Triggered a crash in earlier versions.
  editor_state.ProcessInputString("rei");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("j");

  Clear(&editor_state);

  // Triggered a crash in earlier versions.
  editor_state.ProcessInputString("wr3g");

  Clear(&editor_state);

  // Tests that lines are aligned (based on previous line).
  editor_state.ProcessInputString("i a\nb");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           " a\n b");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia\nb");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kh2w/");

  Clear(&editor_state);

  editor_state.ProcessInputString("af \n");

  Clear(&editor_state);

  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()), "");

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "dwr\n"
      "p"
      "3h");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejo");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 2));
  editor_state.ProcessInputString("p");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alalejoejo");
  editor_state.ProcessInputString("u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejo");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 2));

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejo\nforero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  // One cursor at beginning of each line.
  editor_state.ProcessInputString(
      "g"
      "+"
      "k"
      "_");
  editor_state.ProcessInputString(
      "fo"
      "d\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alej\nfrero");

  Clear(&editor_state);

  // Tests that undoing a delete leaves the cursor at the original position.
  editor_state.ProcessInputString("ialejandro cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "rf "
      "d\n"
      "g"
      "u"
      "i forero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro forero cuervo");

  Clear(&editor_state);

  editor_state.ProcessInputString("3iab");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "ababab");
  editor_state.ProcessInputString(".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "abababababab");
  editor_state.ProcessInputString("u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "ababab");
  editor_state.ProcessInputString("3.");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "abababababababababababab");

  Clear(&editor_state);

  // Test that cursors in the stack of cursors are updated properly.
  editor_state.ProcessInputString("i12345");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("/.\n");  // A cursor in every character.
  editor_state.ProcessInputString(
      "C+"
      "="
      "eialejo");  // Add a new line.
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "C-"
      "_"
      "i-");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejo\n-1-2-3-4-5");

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejandro forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "g"
      "dw\n"
      "l"
      ".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "  cuervo");

  Clear(&editor_state);

  editor_state.ProcessInputString("al");

  Clear(&editor_state);
}

int main(int, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  google::InitGoogleLogging(argv[0]);

  testing::BufferContentsTests();
  testing::LineTests();
  TestCases();
  TreeTestsLong();
  TreeTestsBasic();

  std::cout << "Pass!\n";
  return 0;
}
