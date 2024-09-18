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
  builder.InsertModifier(ColumnNumber(0), LineModifier::kRed);
  builder.InsertModifier(ColumnNumber(1), LineModifier::kGreen);
  builder.InsertModifier(ColumnNumber(2), LineModifier::kBlue);
  builder.InsertModifier(ColumnNumber(3), LineModifier::kBold);
  builder.InsertModifier(ColumnNumber(4), LineModifier::kDim);
  Line line = builder.Copy().Build();

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteSuffix(ColumnNumber(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "al");
    CHECK_EQ(line_copy.modifiers_size(), 2ul);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(0)),
                   LineModifier::kRed);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(1)),
                   LineModifier::kGreen);
  }

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteCharacters(ColumnNumber(1), ColumnNumberDelta(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "ajo");
    CHECK_EQ(line_copy.modifiers_size(), 3ul);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(0)),
                   LineModifier::kRed);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(1)),
                   LineModifier::kBold);
    CheckSingleton(line_copy.modifiers().at(ColumnNumber(2)),
                   LineModifier::kDim);
  }

  // Original isn't modified.
  CHECK_EQ(line.EndColumn(), ColumnNumber(5));
  CHECK_EQ(line.modifiers().size(), 5ul);
  CheckSingleton(line.modifiers().at(ColumnNumber(0)), LineModifier::kRed);
  CheckSingleton(line.modifiers().at(ColumnNumber(1)), LineModifier::kGreen);
  CheckSingleton(line.modifiers().at(ColumnNumber(2)), LineModifier::kBlue);
  CheckSingleton(line.modifiers().at(ColumnNumber(3)), LineModifier::kBold);
  CheckSingleton(line.modifiers().at(ColumnNumber(4)), LineModifier::kDim);
}

void TestLineAppend() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.modifiers().at(ColumnNumber(1)).insert(LineModifier::kRed);
  line.modifiers().at(ColumnNumber(2));

  LineBuilder suffix{SingleLine{LazyString{L"def"}}};
  suffix.InsertModifier(ColumnNumber(1), LineModifier::kBold);
  suffix.set_modifiers(ColumnNumber(2), {});
  line.Append(std::move(suffix));

  CHECK_EQ(line.modifiers().size(), 4ul);
  CHECK(line.modifiers().at(ColumnNumber(1)) ==
        LineModifierSet({LineModifier::kRed}));
  CHECK(line.modifiers().at(ColumnNumber(2)) == LineModifierSet());
  CHECK(line.modifiers().at(ColumnNumber(4)) ==
        LineModifierSet({LineModifier::kBold}));
  CHECK(line.modifiers().at(ColumnNumber(5)) == LineModifierSet());
}

void TestLineAppendEmpty() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.InsertModifier(ColumnNumber(0), LineModifier::kRed);

  line.Append(LineBuilder());

  CHECK_EQ(line.modifiers_size(), 1ul);
  CHECK(line.modifiers().at(ColumnNumber(0)) ==
        LineModifierSet({LineModifier::kRed}));

  line.Append(LineBuilder{SingleLine{LazyString{L"def"}}});

  CHECK_EQ(line.modifiers_size(), 2ul);
  CHECK(line.modifiers().at(ColumnNumber(0)) ==
        LineModifierSet({LineModifier::kRed}));
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
