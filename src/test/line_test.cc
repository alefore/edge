#include "src/test/line_test.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/line.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
template <typename C, typename V>
void CheckSingleton(C const container, V value) {
  CHECK_EQ(container.size(), 1ul);
  CHECK(container.find(value) != container.end());
}

void TestLineDeleteCharacters() {
  // Preparation.
  Line line(Line::Options(NewCopyCharBuffer(L"alejo")));
  line.modifiers()[ColumnNumber(0)].insert(LineModifier::RED);
  line.modifiers()[ColumnNumber(1)].insert(LineModifier::GREEN);
  line.modifiers()[ColumnNumber(2)].insert(LineModifier::BLUE);
  line.modifiers()[ColumnNumber(3)].insert(LineModifier::BOLD);
  line.modifiers()[ColumnNumber(4)].insert(LineModifier::DIM);

  {
    Line::Options line_copy(line);
    line_copy.DeleteSuffix(ColumnNumber(2));
    CHECK_EQ(ToByteString(line_copy.contents->ToString()), "al");
    CHECK_EQ(line_copy.modifiers.size(), 2ul);
    CheckSingleton(line_copy.modifiers[ColumnNumber(0)], LineModifier::RED);
    CheckSingleton(line_copy.modifiers[ColumnNumber(1)], LineModifier::GREEN);
  }

  {
    Line::Options line_copy(line);
    line_copy.DeleteCharacters(ColumnNumber(1), ColumnNumberDelta(2));
    CHECK_EQ(ToByteString(line_copy.contents->ToString()), "ajo");
    CHECK_EQ(line_copy.modifiers.size(), 3ul);
    CheckSingleton(line_copy.modifiers[ColumnNumber(0)], LineModifier::RED);
    CheckSingleton(line_copy.modifiers[ColumnNumber(1)], LineModifier::BOLD);
    CheckSingleton(line_copy.modifiers[ColumnNumber(2)], LineModifier::DIM);
  }

  // Original isn't modified.
  CHECK_EQ(line.EndColumn(), ColumnNumber(5));
  CHECK_EQ(line.modifiers().size(), 5ul);
  CheckSingleton(line.modifiers()[ColumnNumber(0)], LineModifier::RED);
  CheckSingleton(line.modifiers()[ColumnNumber(1)], LineModifier::GREEN);
  CheckSingleton(line.modifiers()[ColumnNumber(2)], LineModifier::BLUE);
  CheckSingleton(line.modifiers()[ColumnNumber(3)], LineModifier::BOLD);
  CheckSingleton(line.modifiers()[ColumnNumber(4)], LineModifier::DIM);
}

void TestLineAppend() {
  Line::Options line;
  line.contents = NewLazyString(L"abc");
  line.modifiers[ColumnNumber(1)].insert(LineModifier::RED);
  line.modifiers[ColumnNumber(2)];

  Line::Options suffix;
  suffix.contents = NewLazyString(L"def");
  suffix.modifiers[ColumnNumber(1)].insert(LineModifier::BOLD);
  suffix.modifiers[ColumnNumber(2)];
  line.Append(Line(suffix));

  CHECK_EQ(line.modifiers.size(), 4ul);
  CHECK(line.modifiers[ColumnNumber(1)] ==
        LineModifierSet({LineModifier::RED}));
  CHECK(line.modifiers[ColumnNumber(2)] == LineModifierSet());
  CHECK(line.modifiers[ColumnNumber(4)] ==
        LineModifierSet({LineModifier::BOLD}));
  CHECK(line.modifiers[ColumnNumber(5)] == LineModifierSet());
}

void TestLineAppendEmpty() {
  Line::Options line;
  line.contents = NewLazyString(L"abc");
  line.modifiers[ColumnNumber(0)].insert(LineModifier::RED);

  Line::Options empty_suffix;
  empty_suffix.contents = NewLazyString(L"");
  line.Append(Line(empty_suffix));

  CHECK_EQ(line.modifiers.size(), 1ul);
  CHECK(line.modifiers[ColumnNumber(0)] ==
        LineModifierSet({LineModifier::RED}));

  Line::Options suffix;
  suffix.contents = NewLazyString(L"def");
  line.Append(Line(suffix));

  CHECK_EQ(line.modifiers.size(), 2ul);
  CHECK(line.modifiers[ColumnNumber(0)] ==
        LineModifierSet({LineModifier::RED}));
  CHECK(line.modifiers[ColumnNumber(3)] == LineModifierSet());
  CHECK_EQ(line.modifiers.size(), 2ul);
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
