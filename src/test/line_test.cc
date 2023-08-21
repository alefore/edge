#include "src/test/line_test.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/line.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
using language::ToByteString;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewCopyCharBuffer;
using language::lazy_string::NewLazyString;

template <typename C, typename V>
void CheckSingleton(C const container, V value) {
  CHECK_EQ(container.size(), 1ul);
  CHECK(container.find(value) != container.end());
}

void TestLineDeleteCharacters() {
  // Preparation.
  Line line(Line::Options(NewCopyCharBuffer(L"alejo")));
  line.modifiers()[ColumnNumber(0)].insert(LineModifier::kRed);
  line.modifiers()[ColumnNumber(1)].insert(LineModifier::kGreen);
  line.modifiers()[ColumnNumber(2)].insert(LineModifier::kBlue);
  line.modifiers()[ColumnNumber(3)].insert(LineModifier::kBold);
  line.modifiers()[ColumnNumber(4)].insert(LineModifier::kDim);

  {
    Line::Options line_copy = line.CopyOptions();
    line_copy.DeleteSuffix(ColumnNumber(2));
    CHECK_EQ(ToByteString(line_copy.contents->ToString()), "al");
    CHECK_EQ(line_copy.modifiers.size(), 2ul);
    CheckSingleton(line_copy.modifiers[ColumnNumber(0)], LineModifier::kRed);
    CheckSingleton(line_copy.modifiers[ColumnNumber(1)], LineModifier::kGreen);
  }

  {
    Line::Options line_copy = line.CopyOptions();
    line_copy.DeleteCharacters(ColumnNumber(1), ColumnNumberDelta(2));
    CHECK_EQ(ToByteString(line_copy.contents->ToString()), "ajo");
    CHECK_EQ(line_copy.modifiers.size(), 3ul);
    CheckSingleton(line_copy.modifiers[ColumnNumber(0)], LineModifier::kRed);
    CheckSingleton(line_copy.modifiers[ColumnNumber(1)], LineModifier::kBold);
    CheckSingleton(line_copy.modifiers[ColumnNumber(2)], LineModifier::kDim);
  }

  // Original isn't modified.
  CHECK_EQ(line.EndColumn(), ColumnNumber(5));
  CHECK_EQ(line.modifiers().size(), 5ul);
  CheckSingleton(line.modifiers()[ColumnNumber(0)], LineModifier::kRed);
  CheckSingleton(line.modifiers()[ColumnNumber(1)], LineModifier::kGreen);
  CheckSingleton(line.modifiers()[ColumnNumber(2)], LineModifier::kBlue);
  CheckSingleton(line.modifiers()[ColumnNumber(3)], LineModifier::kBold);
  CheckSingleton(line.modifiers()[ColumnNumber(4)], LineModifier::kDim);
}

void TestLineAppend() {
  Line::Options line;
  line.contents = NewLazyString(L"abc");
  line.modifiers[ColumnNumber(1)].insert(LineModifier::kRed);
  line.modifiers[ColumnNumber(2)];

  Line::Options suffix;
  suffix.contents = NewLazyString(L"def");
  suffix.modifiers[ColumnNumber(1)].insert(LineModifier::kBold);
  suffix.modifiers[ColumnNumber(2)];
  line.Append(Line(suffix));

  CHECK_EQ(line.modifiers.size(), 4ul);
  CHECK(line.modifiers[ColumnNumber(1)] ==
        LineModifierSet({LineModifier::kRed}));
  CHECK(line.modifiers[ColumnNumber(2)] == LineModifierSet());
  CHECK(line.modifiers[ColumnNumber(4)] ==
        LineModifierSet({LineModifier::kBold}));
  CHECK(line.modifiers[ColumnNumber(5)] == LineModifierSet());
}

void TestLineAppendEmpty() {
  Line::Options line;
  line.contents = NewLazyString(L"abc");
  line.modifiers[ColumnNumber(0)].insert(LineModifier::kRed);

  Line::Options empty_suffix;
  empty_suffix.contents = NewLazyString(L"");
  line.Append(Line(empty_suffix));

  CHECK_EQ(line.modifiers.size(), 1ul);
  CHECK(line.modifiers[ColumnNumber(0)] ==
        LineModifierSet({LineModifier::kRed}));

  Line::Options suffix;
  suffix.contents = NewLazyString(L"def");
  line.Append(Line(suffix));

  CHECK_EQ(line.modifiers.size(), 2ul);
  CHECK(line.modifiers[ColumnNumber(0)] ==
        LineModifierSet({LineModifier::kRed}));
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
