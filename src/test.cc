#include <cassert>
#include <iostream>

#include <glog/logging.h>

#include "editor.h"
#include "terminal.h"

int main(int, char**) {
  using namespace afc::editor;
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
  assert(editor_state.current_buffer()->second->ToString().empty());

  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->contents()->size() == 3);
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
  assert(editor_state.current_buffer()->second->current_position_line() == 3);

  editor_state.ProcessInputString("egg");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == 0);

  editor_state.ProcessInputString("e2d");
  assert(editor_state.current_buffer()->second->contents()->size() == 1);
  assert(editor_state.current_buffer()->second->current_line()->ToString()
         == L"cuervo");

  editor_state.ProcessInputString("pp");
  assert(editor_state.current_buffer()->second->contents()->size() == 5);

  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 5);
  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 0);

  editor_state.ProcessInputString("eel");
  CHECK_EQ(editor_state.current_buffer()->second->current_position_line(), 1);

  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("3d");
  assert(editor_state.current_buffer()->second->current_position_line() == 1);
  assert(editor_state.current_buffer()->second->ToString()
         == L"alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  editor_state.ProcessInputString("ege10d");
  assert(editor_state.current_buffer()->second->ToString() == L"");
  assert(editor_state.current_buffer()->second->contents()->size() == 1);

  editor_state.ProcessInputString("ialejandro forero cuervo\n\n");
  editor_state.ProcessInputString("0123456789abcdefghijklmnopqrstuvwxyz");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("2h2h2h2h2l2l2l2l2l2h2h2h2hegg");
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 0);

  editor_state.ProcessInputString("2l2l2l2l2l");
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("3b");
  assert(editor_state.current_buffer()->second->position().column == 4);

  editor_state.ProcessInputString("2rb");
  assert(editor_state.current_buffer()->second->position().column == 8);

  editor_state.ProcessInputString("200000000rb");
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("eb");
  assert(editor_state.current_buffer()->second->position().line == 2);

  editor_state.ProcessInputString("gf1f3f5f7f9");
  assert(editor_state.current_buffer()->second->position().column == 9);

  editor_state.ProcessInputString("b");
  assert(editor_state.current_buffer()->second->position().column == 7);

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

  // Clear.
  editor_state.ProcessInputString("esg99999999999999999999999d");
  editor_state.ProcessInputString("eeg99999999999999999999999d");
  editor_state.ProcessInput(Terminal::ESCAPE);

  editor_state.ProcessInputString("ihey there hey hey man yes ahoheyblah.");
  assert(editor_state.current_buffer()->second->position().line == 0);
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("glw/");
  assert(editor_state.last_search_query() == L"hey");
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 10);

  // Clear.
  editor_state.ProcessInputString("eeg99999999999999999999999d");
  editor_state.ProcessInput(Terminal::ESCAPE);

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("jjjj");
  editor_state.ProcessInputString("/alejo\n");
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 0);

  // Clear.
  editor_state.ProcessInputString("eeg99999999999999999999999d");
  editor_state.ProcessInput(Terminal::ESCAPE);

  // VM Tests.
  editor_state.ProcessInputString("i0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->position().line == 0);
  assert(editor_state.current_buffer()->second->position().column == 10);

  editor_state.ProcessInputString("acSetPositionColumn(4);;\n");
  assert(editor_state.current_buffer()->second->position().column == 4);
  editor_state.ProcessInputString("acSetPositionColumn(4 - 1);;\n");
  assert(editor_state.current_buffer()->second->position().column == 3);
  editor_state.ProcessInputString("acSetPositionColumn(8 - 2 * 3 + 5);;\n");
  assert(editor_state.current_buffer()->second->position().column == 7);

  std::cout << "Pass!\n";
  return 0;
}
