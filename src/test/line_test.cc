#include "src/test/line_test.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/wstring.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc {
namespace editor {
namespace testing {
namespace {

template <typename C, typename V>
void CheckSingleton(C const container, V value) {
  CHECK_EQ(container.size(), 1ul);
  CHECK(container.contains(value));
}

void TestLineDeleteCharacters() {
  // Preparation.
  LineBuilder builder{SingleLine{LazyString{L"alejo"}}};
  builder.InsertModifier(ColumnNumber(0), LineModifier::Red);
  builder.InsertModifier(ColumnNumber(1), LineModifier::Green);
  builder.InsertModifier(ColumnNumber(2), LineModifier::Blue);
  builder.InsertModifier(ColumnNumber(3), LineModifier::Bold);
  builder.InsertModifier(ColumnNumber(4), LineModifier::Dim);
  Line line = builder.Copy().Build();

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteSuffix(ColumnNumber(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "al");
    CHECK_EQ(line_copy.modifiers_size(), 2ul);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(0)),
                   LineModifier::Red);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(1)),
                   LineModifier::Green);
  }

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteCharacters(ColumnNumber(1), ColumnNumberDelta(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "ajo");
    CHECK_EQ(line_copy.modifiers_size(), 3ul);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(0)),
                   LineModifier::Red);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(1)),
                   LineModifier::Bold);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(2)),
                   LineModifier::Dim);
  }

  // Original isn't modified.
  CHECK_EQ(line.EndColumn(), ColumnNumber(5));
  CHECK_EQ(line.modifiers().size(), 5ul);
  CheckSingleton(line.modifiers().at(ColumnNumber(0)), LineModifier::Red);
  CheckSingleton(line.modifiers().at(ColumnNumber(1)), LineModifier::Green);
  CheckSingleton(line.modifiers().at(ColumnNumber(2)), LineModifier::Blue);
  CheckSingleton(line.modifiers().at(ColumnNumber(3)), LineModifier::Bold);
  CheckSingleton(line.modifiers().at(ColumnNumber(4)), LineModifier::Dim);
}

void TestLineAppend() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.modifiers().at(ColumnNumber(1)).insert(LineModifier::Red);
  line.modifiers().at(ColumnNumber(2));

  LineBuilder suffix{SingleLine{LazyString{L"def"}}};
  suffix.InsertModifier(ColumnNumber(1), LineModifier::Bold);
  suffix.set_modifiers(ColumnNumber(2), {});
  line.Append(std::move(suffix));

  CHECK_EQ(line.modifiers().size(), 4ul);
  CHECK(line.modifiers().at(ColumnNumber(1)) ==
        LineModifierSet({LineModifier::Red}));
  CHECK(line.modifiers().at(ColumnNumber(2)) == LineModifierSet());
  CHECK(line.modifiers().at(ColumnNumber(4)) ==
        LineModifierSet({LineModifier::Bold}));
  CHECK(line.modifiers().at(ColumnNumber(5)) == LineModifierSet());
}

void TestLineAppendEmpty() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.InsertModifier(ColumnNumber(0), LineModifier::Red);

  line.Append(LineBuilder());

  CHECK_EQ(line.modifiers_size(), 1ul);
  CHECK(line.modifiers().at(ColumnNumber(0)) ==
        LineModifierSet({LineModifier::Red}));

  line.Append(LineBuilder{SingleLine{LazyString{L"def"}}});

  CHECK_EQ(line.modifiers_size(), 2ul);
  CHECK(line.modifiers().at(ColumnNumber(0)) ==
        LineModifierSet({LineModifier::Red}));
  CHECK(line.modifiers().at(ColumnNumber(3)) == LineModifierSet());
  CHECK_EQ(line.modifiers_size(), 2ul);
}
}  // namespace

void LineTests() {
  LOG(INFO) << "Line tests: start.";
  TestLineDeleteCharacters();
  TestLineAppend();
  TestLineAppendEmpty();
  LOG(INFO) << "Line tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
