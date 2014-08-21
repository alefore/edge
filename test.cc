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
  assert(editor_state.current_buffer()->second->contents()->empty());

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
  assert(editor_state.current_buffer()->second->current_position_line() == 3);

  editor_state.ProcessInputString("slgg");
  assert(editor_state.current_buffer()->second->current_position_line() == 0);
  assert(editor_state.current_buffer()->second->current_position_col() == 0);

  editor_state.ProcessInputString("sl2d");
  assert(editor_state.current_buffer()->second->contents()->size() == 1);
  assert(editor_state.current_buffer()->second->current_line()->contents->ToString()
         == "cuervo");

  editor_state.ProcessInputString("pp");
  assert(editor_state.current_buffer()->second->contents()->size() == 5);

  std::cout << "Pass!\n";
  return 0;
}
