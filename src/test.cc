#include <cassert>
#include <iostream>
#include <string>

#include <glog/logging.h>

#include "editor.h"
#include "tree.h"
#include "terminal.h"
#include "wstring.h"

using namespace afc::editor;

void CheckIsEmpty(EditorState* editor_state) {
  CHECK_EQ(editor_state->current_buffer()->second->contents()->size(), 1);
  CHECK_EQ(editor_state->current_buffer()->second->contents()->at(0)->size(), 0);
}

void Clear(EditorState* editor_state) {
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->ProcessInputString("eeg99999999999999999999999d");
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->current_buffer()->second
      ->set_bool_variable(OpenBuffer::variable_multiple_cursors(), false);
  editor_state->current_buffer()->second->DestroyOtherCursors();
  editor_state->current_buffer()->second->set_position(LineColumn());
  CheckIsEmpty(editor_state);
}

void ShowList(list<int> l) {
  std::cout << "List:";
  for (auto i : l) { std::cout << " " << i; }
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

void RunRandomTests() {
  auto seed = time(NULL);
  if (getenv("EDGE_TEST_SEED") != nullptr) {
    seed = std::stoll(getenv("EDGE_TEST_SEED"));
  }
  LOG(INFO) << "Seed: " << seed;
  srand(seed);
  EditorState editor_state;
  editor_state.ProcessInputString("i");
  editor_state.ProcessInput(Terminal::ESCAPE);
  for (int i = 0; i < 1000; i++) {
    LOG(INFO) << "Iteration: " << i;
    switch (random() % 4) {
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
    if (random() % 3 == 0) {
      editor_state.ProcessInputString(std::to_string(1 + random() % 5));
    }
    if (random() % 3 == 0) {
      editor_state.ProcessInputString("r");
    }
    switch (random() % 17) {
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
          editor_state.ProcessInputString(strings[random() % strings.size()]);
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
        for (int i = random() % 5; i > 0; --i) {
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
        break;
    }
  }
}

int main(int, char**) {
  EditorState editor_state;
  assert(!editor_state.has_current_buffer());
  editor_state.ProcessInputString("i");
  assert(editor_state.has_current_buffer());
  editor_state.ProcessInputString("alejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("i forero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->current_line()->ToString()
         == L"alejo forero");
  editor_state.ProcessInputString("ed");
  CHECK(editor_state.current_buffer()->second->ToString().empty());

  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 3);
  assert(editor_state.current_buffer()->second->current_position_line() == 2);
  assert(editor_state.current_buffer()->second->current_position_col() == sizeof("cuervo") - 1);
  editor_state.ProcessInputString("ehhh");
  assert(editor_state.current_buffer()->second->current_position_line() == 1);
  assert(editor_state.current_buffer()->second->current_position_col() == sizeof("cuervo") - 1 - 2);

  editor_state.ProcessInputString("k");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  editor_state.ProcessInputString("kkkkk");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);

  editor_state.ProcessInputString("3g");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == 3 - 1);

  editor_state.ProcessInputString("rg");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == sizeof("alejandro") - 1);

  editor_state.ProcessInputString("erg");
  assert(editor_state.current_buffer()->second->current_position_line() == 2);

  editor_state.ProcessInputString("egg");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == 0);

  editor_state.ProcessInputString("e2d");
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 1);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->current_line()->ToString()),
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
  editor_state.ProcessInputString("3d");
  assert(editor_state.current_buffer()->second->current_position_line() == 1);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  editor_state.ProcessInputString("ege10d");
  CHECK(editor_state.current_buffer()->second->ToString() == L"");
  CHECK_EQ(editor_state.current_buffer()->second->contents()->size(), 1);

  editor_state.ProcessInputString("ialejandro forero cuervo\n\n");
  editor_state.ProcessInputString("0123456789abcdefghijklmnopqrstuvwxyz");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("2h2h2h2h2l2l2l2l2l2h2h2h2hegg");
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 0);

  editor_state.ProcessInputString("2l2l2l2l2l");
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("3b");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 4);

  editor_state.ProcessInputString("2rb");
  assert(editor_state.current_buffer()->second->position().column == 8);

  editor_state.ProcessInputString("200000000rb");
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("eb");
  assert(editor_state.current_buffer()->second->position().line == 2);

  editor_state.ProcessInputString("gf1f3f5f7f9");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 9);

  editor_state.ProcessInputString("b");
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 7);

  editor_state.ProcessInputString("10g");
  assert(editor_state.current_buffer()->second->position().column == 9);

  editor_state.ProcessInputString("/123\n");
  assert(editor_state.current_buffer()->second->position().line == 2);
  assert(editor_state.current_buffer()->second->position().column == 1);

  editor_state.ProcessInputString("eg1000000000000000000d");
  assert(editor_state.current_buffer()->second->position().line == 0);

  editor_state.ProcessInputString("ialejo forero\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kg" "3d" "rg" "jp");
  editor_state.ProcessInputString("krg" "j" "rfa");

  Clear(&editor_state);

  editor_state.ProcessInputString("ihey there hey hey man yes ahoheyblah.");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("glw/");
  CHECK(editor_state.last_search_query() == L"hey");
  CHECK_EQ(editor_state.current_buffer()->second->position().line, 0);
  CHECK_EQ(editor_state.current_buffer()->second->position().column, 10);

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("jjjj");
  editor_state.ProcessInputString("/alejo\n");
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 0);

  Clear(&editor_state);

  // VM Tests.
  editor_state.ProcessInputString("i0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("aCSetPositionColumn(4);;\n");
  assert(editor_state.current_buffer()->second->position().column == 4);
  editor_state.ProcessInputString("aCSetPositionColumn(4 - 1);;\n");
  assert(editor_state.current_buffer()->second->position().column == 3);
  editor_state.ProcessInputString("aCSetPositionColumn(8 - 2 * 3 + 5);;\n");
  assert(editor_state.current_buffer()->second->position().column == 7);

  Clear(&editor_state);


  // Test for undo after normal delete line.
  editor_state.ProcessInputString("i12345\n67890");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "12345\n67890");

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->second->position(), LineColumn(0, 0));

  editor_state.ProcessInputString("e5d");
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
  editor_state.ProcessInputString("kg5~");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "ALEjanDRo\nfoRero");
  editor_state.ProcessInputString("w]~");
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

  editor_state.ProcessInputString("w[d");
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
  editor_state.ProcessInputString("avmultiple_cursors\n");
  CHECK(editor_state.current_buffer()->second
            ->read_bool_variable(OpenBuffer::variable_multiple_cursors()));

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

  editor_state.ProcessInputString("d" "l" "rd" "l");
  editor_state.ProcessInputString("i)");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "al[a)ndro fo[r)o cu[v)o");

  Clear(&editor_state);

  editor_state.ProcessInputString("ioo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("/o\n" "cl" "-");

  Clear(&editor_state);

  editor_state.ProcessInputString("i\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k" "~");
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
  editor_state.ProcessInputString("2l" "+");  // Cursors: 0, 2, *2
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
  editor_state.ProcessInputString("ed");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "01\n23\n67\n89\n");
  editor_state.ProcessInputString(".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "01\n23\n89\n");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("h");
  editor_state.ProcessInputString("w[d");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->second->ToString()),
           "a");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia\nbcd");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k" "w[d");
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

  TreeTestsLong();
  TreeTestsBasic();

  RunRandomTests();

  std::cout << "Pass!\n";
  return 0;
}
