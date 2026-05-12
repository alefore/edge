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

using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LinePartMetadata;

namespace afc::editor::testing {
namespace {

void TestLineDeleteCharacters() {
  // Preparation.
  LineBuilder builder{SingleLine{LazyString{L"alejo"}}};
  builder.InsertModifiers(
      ColumnNumber(0),
      LinePartMetadata{.style = Style{.foreground_color = StandardColor::Red}});
  builder.InsertModifiers(
      ColumnNumber(1),
      LinePartMetadata{.style =
                           Style{.foreground_color = StandardColor::Green}});
  builder.InsertModifiers(
      ColumnNumber(2),
      LinePartMetadata{.style =
                           Style{.foreground_color = StandardColor::Blue}});
  builder.InsertModifiers(
      ColumnNumber(3),
      LinePartMetadata{.style = Style{.attributes = StyleAttribute::Bold}});
  builder.InsertModifiers(
      ColumnNumber(4),
      LinePartMetadata{.style = Style{.attributes = StyleAttribute::Dim}});
  Line line = builder.Copy().Build();

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteSuffix(ColumnNumber(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "al");
    CHECK_EQ(line_copy.modifiers_size(), 2ul);
    CHECK_EQ(line_copy.modifiers().at(ColumnNumber(0)).style,
             Style{.foreground_color = StandardColor::Red});
    (line_copy.modifiers().at(ColumnNumber(1)), StandardColor::Green);
  }

  {
    LineBuilder line_copy = builder.Copy();
    line_copy.DeleteCharacters(ColumnNumber(1), ColumnNumberDelta(2));
    CHECK_EQ(line_copy.Copy().Build().contents().ToBytes(), "ajo");
    CHECK_EQ(line_copy.modifiers_size(), 3ul);
    CHECK_EQ(line_copy.modifiers().at(ColumnNumber(0)).style,
             Style{.foreground_color = StandardColor::Red});
    CHECK_EQ(line_copy.modifiers().at(ColumnNumber(1)).style,
             Style{.attributes = StyleAttribute::Bold});
    CHECK_EQ(line_copy.modifiers().at(ColumnNumber(2)).style,
             Style{.attributes = StyleAttribute::Dim});
  }

  // Original isn't modified.
  CHECK_EQ(line.EndColumn(), ColumnNumber(5));
  CHECK_EQ(line.modifiers().size(), 5ul);
  CHECK_EQ(line.modifiers().at(ColumnNumber(0)).style,
           Style{.foreground_color = StandardColor::Red});
  CHECK_EQ(line.modifiers().at(ColumnNumber(1)).style,
           Style{.foreground_color = StandardColor::Green});
  CHECK_EQ(line.modifiers().at(ColumnNumber(2)).style,
           Style{.foreground_color = StandardColor::Blue});
  CHECK_EQ(line.modifiers().at(ColumnNumber(3)).style,
           Style{.attributes = StyleAttribute::Bold});
  CHECK_EQ(line.modifiers().at(ColumnNumber(4)).style,
           Style{.attributes = StyleAttribute::Dim});
}

void TestLineAppend() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.modifiers().at(ColumnNumber(1)).style.foreground_color =
      StandardColor::Red;
  line.modifiers().at(ColumnNumber(2));

  LineBuilder suffix{SingleLine{LazyString{L"def"}}};
  suffix.InsertModifiers(
      ColumnNumber(1),
      LinePartMetadata{.style = Style{.attributes = StyleAttribute::Bold}});
  suffix.set_modifiers(ColumnNumber(2), {});
  line.Append(std::move(suffix));

  CHECK_EQ(line.modifiers().size(), 4ul);
  CHECK_EQ(line.modifiers().at(ColumnNumber(1)).style,
           Style({StandardColor::Red}));
  CHECK_EQ(line.modifiers().at(ColumnNumber(2)).style, Style{});
  CHECK_EQ(line.modifiers().at(ColumnNumber(4)).style,
           Style{.attributes = StyleAttribute::Bold});
  CHECK_EQ(line.modifiers().at(ColumnNumber(5)).style, Style{});
}

void TestLineAppendEmpty() {
  LineBuilder line{SingleLine{LazyString{L"abc"}}};
  line.InsertModifiers(
      ColumnNumber(0),
      LinePartMetadata{.style = Style{.foreground_color = StandardColor::Red}});

  line.Append(LineBuilder());

  CHECK_EQ(line.modifiers_size(), 1ul);
  CHECK_EQ(line.modifiers().at(ColumnNumber(0)).style,
           Style{.foreground_color = StandardColor::Red});

  line.Append(LineBuilder{SingleLine{LazyString{L"def"}}});

  CHECK_EQ(line.modifiers_size(), 2ul);
  CHECK_EQ(line.modifiers().at(ColumnNumber(0)).style,
           Style{.foreground_color = StandardColor::Red});
  CHECK_EQ(line.modifiers().at(ColumnNumber(3)).style, Style{});
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

}  // namespace afc::editor::testing
