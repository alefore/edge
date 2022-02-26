#include <glog/logging.h>

#include <csignal>
#include <iostream>
#include <string>

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/const_tree.h"
#include "src/editor.h"
#include "src/terminal.h"
#include "src/test/buffer_contents_test.h"
#include "src/test/line_test.h"
#include "src/wstring.h"

using namespace afc::editor;

bool IsEmpty(EditorState* editor_state) {
  return editor_state->current_buffer()->EndLine() == LineNumber(0) &&
         editor_state->current_buffer()
             ->contents()
             ->back()
             ->EndColumn()
             .IsZero();
}

void Clear(EditorState* editor_state) {
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->set_current_buffer(
      editor_state->buffers()->find(BufferName(L"anonymous buffer 0"))->second,
      CommandArgumentModeApplyMode::kFinal);

  editor_state->ProcessInputString("eegde999999999999999\n");
  editor_state->ProcessInput(Terminal::ESCAPE);
  editor_state->current_buffer()->Set(buffer_variables::multiple_cursors,
                                      false);
  editor_state->current_buffer()->DestroyOtherCursors();
  editor_state->current_buffer()->set_position(LineColumn());
  CHECK(IsEmpty(editor_state));
}

void ShowList(list<int> l) {
  std::cout << "List:";
  for (auto i : l) {
    std::cout << " " << i;
  }
  std::cout << "\n";
}

list<int> ToList(ConstTree<int>::Ptr tree) {
  using T = ConstTree<int>;
  list<int> output;
  CHECK(T::Every(tree, [&](int v) {
    output.push_back(v);
    return true;
  }));
  CHECK_EQ(output.size(), T::Size(tree));
  return output;
}

void TreeTestsLong() {
  srand(0);
  list<int> l;
  using T = ConstTree<int>;
  T::Ptr t;
  size_t elements = 500;
  for (size_t i = 0; i < elements; i++) {
    int position = rand() % (1 + T::Size(t));
    auto l_it = l.begin();
    std::advance(l_it, position);
    l.insert(l_it, i);
    t = T::Append(T::PushBack(T::Prefix(t, position), i),
                  T::Suffix(t, position));
    CHECK(ToList(t) == l);
  }

  LOG(INFO) << "Starting delete tests.";
  for (size_t i = 0; i < elements / 2; i++) {
    int position = rand() % T::Size(t);
    auto l_it = l.begin();
    std::advance(l_it, position);

    LOG(INFO) << "Erasing at position " << position << ": ";
    l.erase(l_it);
    t = T::Append(T::Prefix(t, position), T::Suffix(t, position + 1));
    CHECK(ToList(t) == l);
  }
}

void TreeTestsBasic() {
  using T = ConstTree<int>;

  T::Ptr t;
  CHECK_EQ(T::Size(t), 0ul);

  t = T::Leaf(10);
  CHECK_EQ(T::Size(t), 1ul);
  CHECK_EQ(t->Get(0), 10);

  t = T::PushBack(t, 20);
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(1), 20);
  CHECK_EQ(T::Size(t), 2ul);

  t = T::PushBack(t, 30);
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(2), 30);
  CHECK_EQ(T::Size(t), 3ul);

  t = T::PushBack(t, 40);
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(3), 40);
  CHECK_EQ(T::Size(t), 4ul);

  t = T::Append(T::Leaf(5), t);
  CHECK_EQ(t->Get(0), 5);
  CHECK_EQ(t->Get(1), 10);
  CHECK_EQ(t->Get(2), 20);
  CHECK_EQ(t->Get(3), 30);
  CHECK_EQ(t->Get(4), 40);
}

void TestCases() {
  auto audio_player = NewNullAudioPlayer();
  EditorState editor_state(CommandLineValues(), audio_player.get());
  CHECK(!editor_state.has_current_buffer());

  editor_state.ProcessInputString("i\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK(editor_state.has_current_buffer());
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "\n");
  editor_state.ProcessInputString("ib");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "\nb");
  editor_state.ProcessInputString(".u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "\nb");

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
  CHECK(editor_state.current_buffer()->current_line()->ToString() ==
        L"alejo forero");
  editor_state.ProcessInputString("gde\n");
  CHECK(editor_state.current_buffer()->ToString().empty());

  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->contents()->size(),
           LineNumberDelta(3));
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(2));
  CHECK_EQ(editor_state.current_buffer()->current_position_col(),
           ColumnNumber(sizeof("cuervo") - 1));
  editor_state.ProcessInputString("ehhh");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(1));
  CHECK_EQ(editor_state.current_buffer()->current_position_col(),
           ColumnNumber(sizeof("cuervo") - 1 - 2));

  editor_state.ProcessInputString("k");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));
  editor_state.ProcessInputString("kkkkk");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));

  editor_state.ProcessInputString("3g");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->current_position_col(),
           ColumnNumber(3 - 1));

  editor_state.ProcessInputString("rg");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->current_position_col(),
           ColumnNumber(sizeof("alejandro") - 1));

  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(2));

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->current_position_col(),
           ColumnNumber(0));

  editor_state.ProcessInputString("d2e]\n");
  CHECK_EQ(
      ToByteString(editor_state.current_buffer()->current_line()->ToString()),
      "cuervo");

  editor_state.ProcessInputString("pp");
  CHECK_EQ(editor_state.current_buffer()->contents()->size(),
           LineNumberDelta(5));

  editor_state.ProcessInputString("erg");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(4));
  editor_state.ProcessInputString("eg");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(0));

  editor_state.ProcessInputString("eel");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(1));

  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("d3\n");
  CHECK_EQ(editor_state.current_buffer()->current_position_line(),
           LineNumber(1));
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  Clear(&editor_state);

  editor_state.ProcessInputString("ialejandro forero cuervo\n\n");
  editor_state.ProcessInputString("0123456789abcdefghijklmnopqrstuvwxyz");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("2h2h2h2h2l2l2l2l2l2h2h2h2hegg");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(0));

  editor_state.ProcessInputString("2l2l2l2l2l");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(10));

  editor_state.ProcessInputString("3b");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(4));

  editor_state.ProcessInputString("2rb");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(8));

  editor_state.ProcessInputString("eb");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(2));

  editor_state.ProcessInputString("gf1f3f5f7f9");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(9));

  editor_state.ProcessInputString("b");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(7));

  editor_state.ProcessInputString("10g");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(9));

  editor_state.ProcessInputString("/123\n");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(2));
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(1));

  editor_state.ProcessInputString("egd1000000000000000000\n");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));

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
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("gw/");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(10));

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("jjjj");
  editor_state.ProcessInputString("/alejo\n");
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(0));

  Clear(&editor_state);

  // VM Tests.
  editor_state.ProcessInputString("i0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->position().line, LineNumber(0));
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(10));

  editor_state.ProcessInputString("aCSetPositionColumn(4);;\n");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(4));
  editor_state.ProcessInputString("aCSetPositionColumn(4 - 1);;\n");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(3));
  editor_state.ProcessInputString("aCSetPositionColumn(8 - 2 * 3 + 5);;\n");
  CHECK_EQ(editor_state.current_buffer()->position().column, ColumnNumber(7));

  Clear(&editor_state);

  // Test for undo after normal delete line.
  editor_state.ProcessInputString("i12345\n67890");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "12345\n67890");

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->position(), LineColumn());

  editor_state.ProcessInputString("de5\n");
  CHECK(IsEmpty(&editor_state));

  editor_state.ProcessInput('u');
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "12345\n67890");

  Clear(&editor_state);

  // Test for insertion at EOF.
  CHECK_EQ(editor_state.current_buffer()->EndLine(), LineNumber(0));
  editor_state.ProcessInputString("55ji\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(editor_state.current_buffer()->EndLine(), LineNumber(1));

  Clear(&editor_state);

  // Test for uppercase switch
  editor_state.ProcessInputString("ialeJAnDRo\nfoRero");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kg~5\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "ALEjanDRo\nfoRero");
  editor_state.ProcessInputString("~W\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "ALEjaNdrO\nfoRero");

  Clear(&editor_state);

  // Test that delete word across multiple lines works.
  editor_state.ProcessInputString("ialejandro\n\n\n\n  forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandro\n\n\n\n  forero cuervo");

  editor_state.ProcessInputString("egg");
  CHECK_EQ(editor_state.current_buffer()->position(), LineColumn());

  editor_state.ProcessInputString("rg");
  CHECK_EQ(editor_state.current_buffer()->position(),
           LineColumn(LineNumber(0), ColumnNumber(9)));

  editor_state.ProcessInputString("dw)\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandroforero cuervo");

  Clear(&editor_state);

  // Test multiple cursors.
  editor_state.ProcessInputString("ialejandro\nforero\ncuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandro\nforero\ncuervo");

  editor_state.ProcessInputString("g");
  CHECK_EQ(editor_state.current_buffer()->position(),
           LineColumn(LineNumber(2)));

  editor_state.ProcessInputString("+eg");
  CHECK_EQ(editor_state.current_buffer()->position(), LineColumn());

  editor_state.ProcessInputString("w+");
  editor_state.ProcessInputString("_");
  CHECK(
      editor_state.current_buffer()->Read(buffer_variables::multiple_cursors));

  editor_state.ProcessInputString("i1234 ");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "1234 alejandro\n1234 forero\n1234 cuervo");
  Clear(&editor_state);

  // Test multiple cursors in same line, movement.
  LOG(INFO) << "Multiple cursors test: start";
  editor_state.ProcessInputString("ialejandro forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandro forero cuervo");
  editor_state.ProcessInputString("rfc+gw+");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("avmultiple_cursors\n");
  editor_state.ProcessInputString("ll");
  editor_state.ProcessInputString("i[");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "al[ejandro fo[rero cu[ervo");

  editor_state.ProcessInputString(
      "d\n"
      "l"
      "dr\n"
      "l");
  editor_state.ProcessInputString("i)");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "123\n456\n789");
  editor_state.ProcessInputString("+");    // Leave a cursor at 5.
  editor_state.ProcessInputString("kll");  // Leave cursor at end of first line.
  // Bugs happen here! Did the cursors get adjusted?
  editor_state.ProcessInputString("d\n");
  editor_state.ProcessInputString(
      "_"
      "ix");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "\n");

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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "01-23456789");

  Clear(&editor_state);

  // Behavior with moving past end of line.
  editor_state.ProcessInputString("i0123\n0123456789");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("k3h");
  CHECK_EQ(editor_state.current_buffer()->position(),
           LineColumn(LineNumber(), ColumnNumber(1)));

  Clear(&editor_state);

  editor_state.ProcessInputString("i01\n23\n45\n67\n89\n");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("3k");  // Cursor at line "45".
  editor_state.ProcessInputString("de]\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "01\n23\n67\n89\n");
  editor_state.ProcessInputString(".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "01\n23\n89\n");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("h");
  editor_state.ProcessInputString("d)\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "a");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia\nbcd");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "k"
      "dW)\n");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "abcd");

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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), " a\n b");

  Clear(&editor_state);

  editor_state.ProcessInputString("ia\nb");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString("kh2w/");

  Clear(&editor_state);

  editor_state.ProcessInputString("af \n");

  Clear(&editor_state);

  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "");

  editor_state.ProcessInputString("ialejo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "dwr\n"
      "p"
      "3h");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "alejo");
  CHECK_EQ(editor_state.current_buffer()->position(),
           LineColumn(LineNumber(0), ColumnNumber(2)));
  editor_state.ProcessInputString("p");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alalejoejo");
  editor_state.ProcessInputString("u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "alejo");
  CHECK_EQ(editor_state.current_buffer()->position(),
           LineColumn(LineNumber(0), ColumnNumber(2)));

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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejandro forero cuervo");

  Clear(&editor_state);

  editor_state.ProcessInputString("3iab");
  editor_state.ProcessInput(Terminal::ESCAPE);
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "ababab");
  editor_state.ProcessInputString(".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "abababababab");
  editor_state.ProcessInputString("u");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "ababab");
  editor_state.ProcessInputString("3.");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
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
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()),
           "alejo\n-1-2-3-4-5");

  Clear(&editor_state);

  editor_state.ProcessInputString("ialejandro forero cuervo");
  editor_state.ProcessInput(Terminal::ESCAPE);
  editor_state.ProcessInputString(
      "g"
      "dw\n"
      "l"
      ".");
  CHECK_EQ(ToByteString(editor_state.current_buffer()->ToString()), "  cuervo");

  Clear(&editor_state);

  editor_state.ProcessInputString("al");

  Clear(&editor_state);
}

int main(int, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  google::InitGoogleLogging(argv[0]);

  testing::BufferContentsTests();
  testing::LineTests();
  LOG(INFO) << "Basic tests";
  TestCases();
  LOG(INFO) << "TreeTestsLong";
  TreeTestsLong();
  LOG(INFO) << "TreeTestsBasic";
  TreeTestsBasic();

  std::cout << "Pass!\n";
  return 0;
}
