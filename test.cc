#include <cassert>
#include <iostream>

#include "editor.h"
#include "terminal.h"

int main(int argc, char**argv) {
  using namespace afc::editor;
  EditorState editor_state;
  assert(!editor_state.has_current_buffer());
  editor_state.ProcessInputString("i");
  assert(editor_state.has_current_buffer());
  editor_state.ProcessInputString("alejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("i forero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->current_line()->contents->ToString()
         == "alejo forero");
  editor_state.ProcessInputString("sld");
  assert(editor_state.current_buffer()->second->ToString().empty());

  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  assert(editor_state.current_buffer()->second->contents()->size() == 3);
  assert(editor_state.current_buffer()->second->current_position_line() == 2);
  assert(editor_state.current_buffer()->second->current_position_col() == sizeof("cuervo") - 1);
  editor_state.ProcessInputString("slhhh");
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

  editor_state.ProcessInputString("slrg");
  assert(editor_state.current_buffer()->second->current_position_line() == 2);

  editor_state.ProcessInputString("slgg");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == 0);

  editor_state.ProcessInputString("sl2d");
  assert(editor_state.current_buffer()->second->contents()->size() == 1);
  assert(editor_state.current_buffer()->second->current_line()->contents->ToString()
         == "cuervo");

  editor_state.ProcessInputString("pp");
  assert(editor_state.current_buffer()->second->contents()->size() == 5);

  editor_state.ProcessInputString("slrg");
  assert(editor_state.current_buffer()->second->current_position_line() == 4);

  editor_state.ProcessInputString("sLhhh");
  assert(editor_state.current_buffer()->second->current_position_line() == 1);

  editor_state.ProcessInputString("sc3d");
  assert(editor_state.current_buffer()->second->current_position_line() == 1);
  assert(editor_state.current_buffer()->second->ToString()
         == "alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  editor_state.ProcessInputString("slgsl10d");
  assert(editor_state.current_buffer()->second->ToString() == "");
  assert(editor_state.current_buffer()->second->contents()->size() == 1);

  editor_state.ProcessInputString("ialejandro forero cuervo\n\n");
  editor_state.ProcessInputString("0123456789abcdefghijklmnopqrstuvwxyz");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("2h2h2h2h2l2l2l2l2l2h2h2h2hslgg");
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

  editor_state.ProcessInputString("slb");
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

  editor_state.ProcessInputString("slg1000000000000000000d");
  for (int i = 0; i < 5; i++) {
    editor_state.ProcessInputString("b");
    assert(editor_state.current_buffer()->second->position().line == 0);
  }

  editor_state.ProcessInputString("ialejo forero\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kg3drgjp");

  std::cout << "Pass!\n";
  return 0;
}
