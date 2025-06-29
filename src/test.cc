#include <glog/logging.h>

#include <csignal>
#include <iostream>
#include <string>

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/infrastructure/audio.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/const_tree.h"
#include "src/language/wstring.h"
#include "src/terminal.h"
#include "src/test/buffer_contents_test.h"
#include "src/test/line_test.h"

using namespace afc::editor;
using afc::infrastructure::ControlChar;
using afc::infrastructure::VectorExtendedChar;
using afc::language::ConstTree;
using afc::language::VectorBlock;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

namespace audio = afc::infrastructure::audio;

using IntConstTree = ConstTree<VectorBlock<int, 128>, 128>;

bool IsEmpty(EditorState* editor_state) {
  return editor_state->current_buffer()->ptr()->EndLine() == LineNumber(0) &&
         editor_state->current_buffer()
             ->ptr()
             ->contents()
             .back()
             .EndColumn()
             .IsZero();
}

void Clear(EditorState* editor_state) {
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->set_current_buffer(
      editor_state->buffer_registry()
          .Find(BufferName(LazyString{L"anonymous buffer 0"}))
          .value(),
      CommandArgumentModeApplyMode::kFinal);

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"eegde999999999999999\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->current_buffer()->ptr()->Set(buffer_variables::multiple_cursors,
                                             false);
  editor_state->current_buffer()->ptr()->DestroyOtherCursors();
  editor_state->current_buffer()->ptr()->set_position(LineColumn());
  CHECK(IsEmpty(editor_state));
}

void ShowList(std::list<int> l) {
  std::cout << "List:";
  for (auto i : l) {
    std::cout << " " << i;
  }
  std::cout << "\n";
}

std::list<int> ToList(IntConstTree::Ptr tree) {
  using T = IntConstTree;
  std::list<int> output;
  CHECK(T::Every(tree, [&](int v) {
    output.push_back(v);
    return true;
  }));
  CHECK_EQ(output.size(), T::Size(tree));
  return output;
}

void TreeTestsLong() {
  srand(0);
  std::list<int> l;
  using T = IntConstTree;
  T::Ptr t;
  size_t elements = 500;
  for (size_t i = 0; i < elements; i++) {
    int position = rand() % (1 + T::Size(t));
    auto l_it = l.begin();
    std::advance(l_it, position);
    l.insert(l_it, i);
    t = T::Append(T::PushBack(T::Prefix(t, position), i).get_shared(),
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
  using T = IntConstTree;

  T::Ptr t;
  CHECK_EQ(T::Size(t), 0ul);

  t = T::Leaf(10).Share().get_shared();
  CHECK_EQ(T::Size(t), 1ul);
  CHECK_EQ(t->Get(0), 10);

  t = T::PushBack(t, 20).get_shared();
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(1), 20);
  CHECK_EQ(T::Size(t), 2ul);

  t = T::PushBack(t, 30).get_shared();
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(2), 30);
  CHECK_EQ(T::Size(t), 3ul);

  t = T::PushBack(t, 40).get_shared();
  CHECK_EQ(t->Get(0), 10);
  CHECK_EQ(t->Get(3), 40);
  CHECK_EQ(T::Size(t), 4ul);

  t = T::Append(T::Leaf(5).Share().get_shared(), t);
  CHECK_EQ(t->Get(0), 5);
  CHECK_EQ(t->Get(1), 10);
  CHECK_EQ(t->Get(2), 20);
  CHECK_EQ(t->Get(3), 30);
  CHECK_EQ(t->Get(4), 40);
}

void TestCases() {
  auto audio_player = audio::NewNullPlayer();
  auto editor_state =
      EditorState::New(CommandLineValues(), audio_player.value());
  CHECK(!editor_state->has_current_buffer());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK(editor_state->has_current_buffer());
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "\n");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ib"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"k"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "\nb");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L".u"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "\nb");

  // Caused a crash (found by fuzz testing).
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"5i\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"+"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"3k"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"iblah"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"+_"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"j."}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"u"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i"}));
  editor_state->ProcessInput({ControlChar::kBackspace, ControlChar::kEscape});

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i"}));
  CHECK(editor_state->has_current_buffer());
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"alejo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i forero"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK(editor_state->current_buffer()
            ->ptr()
            ->OptionalCurrentLine()
            ->ToString() == L"alejo forero");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"gde\n"}));
  CHECK(editor_state->current_buffer()->ptr()->ToString().empty());

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro\nforero\ncuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->contents().size(),
           LineNumberDelta(3));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(2));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_col(),
           ColumnNumber(sizeof("cuervo") - 1));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ehhh"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(1));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_col(),
           ColumnNumber(sizeof("cuervo") - 1 - 2));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"k"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"kkkkk"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"3g"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_col(),
           ColumnNumber(3 - 1));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"rg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_col(),
           ColumnNumber(sizeof("alejandro") - 1));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"erg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(2));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"egg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_col(),
           ColumnNumber(0));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"d2e]\n"}));
  CHECK_EQ(editor_state->current_buffer()
               ->ptr()
               ->OptionalCurrentLine()
               ->contents()
               .ToBytes(),
           "cuervo");

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"pp"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->contents().size(),
           LineNumberDelta(5));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"erg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(4));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"eg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(0));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"eel"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(1));

  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"d3\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->current_position_line(),
           LineNumber(1));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandro\nero\nalejandro\nforero\ncuervo");

  // Clear it all.
  Clear(&editor_state.value());

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro forero cuervo\n\n"}));
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"0123456789abcdefghijklmnopqrstuvwxyz"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"2h2h2h2h2l2l2l2l2l2h2h2h2hegg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(0));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"2l2l2l2l2l"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(10));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"3b"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(4));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"2rb"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(8));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"eb"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(2));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"gf1f3f5f7f9"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(9));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"b"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(7));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"10g"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(9));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"/123\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(2));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(1));

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"egd1000000000000000000\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejo forero\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"kg"
                                    L"d3\n"
                                    L"rg"
                                    L"jp"}));
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"krg"
                                    L"j"
                                    L"rfa"}));

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(
      LazyString{L"ihey there hey hey man yes ahoheyblah."}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"gw/"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(10));

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ialejo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"jjjj"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"/alejo\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(0));

  Clear(&editor_state.value());

  // VM Tests.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i0123456789"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().line,
           LineNumber(0));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(10));

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"aCSetPositionColumn(4);;\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(4));
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"aCSetPositionColumn(4 - 1);;\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(3));
  editor_state->ProcessInput(VectorExtendedChar(
      LazyString{L"aCSetPositionColumn(8 - 2 * 3 + 5);;\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position().column,
           ColumnNumber(7));

  Clear(&editor_state.value());

  // Test for undo after normal delete line.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i12345\n67890"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "12345\n67890");

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"egg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(), LineColumn());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"de5\n"}));
  CHECK(IsEmpty(&editor_state.value()));

  editor_state->ProcessInput({L'u'});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "12345\n67890");

  Clear(&editor_state.value());

  // Test for insertion at EOF.
  CHECK_EQ(editor_state->current_buffer()->ptr()->EndLine(), LineNumber(0));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"55ji\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->EndLine(), LineNumber(1));

  Clear(&editor_state.value());

  // Test for uppercase switch
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialeJAnDRo\nfoRero"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"kg~5\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "ALEjanDRo\nfoRero");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"~W\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "ALEjaNdrO\nfoRero");

  Clear(&editor_state.value());

  // Test that delete word across multiple lines works.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro\n\n\n\n  forero cuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandro\n\n\n\n  forero cuervo");

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"egg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(), LineColumn());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"rg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(),
           LineColumn(LineNumber(0), ColumnNumber(9)));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"dw)\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandroforero cuervo");

  Clear(&editor_state.value());

  // Test multiple cursors.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro\nforero\ncuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandro\nforero\ncuervo");

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"g"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(),
           LineColumn(LineNumber(2)));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"+eg"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(), LineColumn());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"w+"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"_"}));
  CHECK(editor_state->current_buffer()->ptr()->Read(
      buffer_variables::multiple_cursors));

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i1234 "}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "1234 alejandro\n1234 forero\n1234 cuervo");
  Clear(&editor_state.value());

  // Test multiple cursors in same line, movement.
  LOG(INFO) << "Multiple cursors test: start";
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro forero cuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandro forero cuervo");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"rfc+gw+"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"avmultiple_cursors\n"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ll"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i["}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "al[ejandro fo[rero cu[ervo");

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"d\n"
                                    L"l"
                                    L"dr\n"
                                    L"l"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i)"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "al[a)ndro fo[r)o cu[v)o");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i123\n56\n789"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput({L'h', L'+'});  // Leave a cursor at 9.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"khh"}));  // Cursor at 5).
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i4"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "123\n456\n789");
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"+"}));  // Leave a cursor at 5).
  editor_state->ProcessInput(VectorExtendedChar(
      LazyString{L"kll"}));  // Leave cursor at end of first line).
  // Bugs happen here! Did the cursors get adjusted?
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"d\n"}));
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"_"
                                    L"ix"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "123x4x56\n78x9");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ioo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"/o\n"
                                    L"cl"
                                    L"-"}));

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput({L'k', L'~'});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "\n");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i\n-"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"k~"}));

  Clear(&editor_state.value());

  // Can cancel the search prompt.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"/"}));
  editor_state->ProcessInput({ControlChar::kEscape});

  Clear(&editor_state.value());

  // Search switching cursors.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i0123456789"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput({L'g'});
  editor_state->ProcessInput({L'+'});  // Cursors: 0, *)0
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"2l"
                                    L"+"}));  // Cursors: 0, 2, *2
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"2l"}));  // Cursors: 0, 2, *)4
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ch"}));  // Cursors: 0, *2, )4
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i-"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "01-23456789");

  Clear(&editor_state.value());

  // Behavior with moving past end of line.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"i0123\n0123456789"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"k3h"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(),
           LineColumn(LineNumber(), ColumnNumber(1)));

  Clear(&editor_state.value());

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"i01\n23\n45\n67\n89\n"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"3k"}));  // Cursor at line "45").
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"de]\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "01\n23\n67\n89\n");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"."}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "01\n23\n89\n");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ia"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"h"}));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"d)\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "a");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ia\nbcd"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"k"
                                    L"dW)\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "abcd");

  Clear(&editor_state.value());

  // Triggered a crash in earlier versions.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"rei"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput({L'j'});

  Clear(&editor_state.value());

  // Triggered a crash in earlier versions.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"wr3g"}));

  Clear(&editor_state.value());

  // Tests that lines are aligned (based on previous line).
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i a\nb"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           " a\n b");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ia\nb"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"kh2w/"}));

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"af \n"}));

  Clear(&editor_state.value());

  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(), "");

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ialejo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"dwr\n"
                                    L"p"
                                    L"3h"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejo");
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(),
           LineColumn(LineNumber(0), ColumnNumber(2)));
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"p"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alalejoejo");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"u"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejo");
  CHECK_EQ(editor_state->current_buffer()->ptr()->position(),
           LineColumn(LineNumber(0), ColumnNumber(2)));

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"ialejo\nforero"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  // One cursor at beginning of each line.
  editor_state->ProcessInput({L'g', L'+', L'k', L'_'});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"fo"
                                    L"d\n"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alej\nfrero");

  Clear(&editor_state.value());

  // Tests that undoing a delete leaves the cursor at the original
  // position.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro cuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"rf "
                                    L"d\n"
                                    L"g"
                                    L"u"
                                    L"i forero"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejandro forero cuervo");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"3iab"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "ababab");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"."}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "abababababab");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"u"}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "ababab");
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"3."}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "abababababababababababab");

  Clear(&editor_state.value());

  // Test that cursors in the stack of cursors are updated properly.
  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"i12345"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"/.\n"}));  // A cursor in every character.
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"C+"
                                    L"="
                                    L"eialejo"}));  // Add a new line.
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput({L'C', L'-', L'_', L'i', L'-'});
  editor_state->ProcessInput({ControlChar::kEscape});
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "alejo\n-1-2-3-4-5");

  Clear(&editor_state.value());

  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"ialejandro forero cuervo"}));
  editor_state->ProcessInput({ControlChar::kEscape});
  editor_state->ProcessInput(
      VectorExtendedChar(LazyString{L"g"
                                    L"dw\n"
                                    L"l"
                                    L"."}));
  CHECK_EQ(editor_state->current_buffer()->ptr()->ToString().ToBytes(),
           "  cuervo");

  Clear(&editor_state.value());

  editor_state->ProcessInput(VectorExtendedChar(LazyString{L"al"}));

  Clear(&editor_state.value());
}

int main(int, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  google::InitGoogleLogging(argv[0]);

  testing::MutableLineSequenceTests();
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
