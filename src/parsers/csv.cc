#include "src/parsers/csv.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/language/text/line_sequence.h"
#include "src/parse_tools.h"
#include "src/parsers/util.h"
#include "src/seek.h"

namespace afc::editor::parsers {
namespace {
using afc::infrastructure::screen::StyleAttribute;
using infrastructure::screen::Color;
using infrastructure::screen::Style;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::Range;

enum State { DEFAULT, CSV_ROW, CSV_CELL };

// TODO(2026-05-04, P2): We could use ParseTree::PropertyMap much better now,
// given that we now support values. So StringValue and NumberValue could be
// combined into a CellFormat property, and TableCell could become a single
// property with the csv_column number as a value.
class CsvParser : public LineOrientedTreeParser {
 protected:
  void ParseLine(ParseData* result) override {
    SkipSpaces(result);
    result->Push(CSV_ROW, ColumnNumberDelta(), {}, {});
    for (size_t row = 0; result->seek().read() != L'\n'; row++) {
      VLOG(10) << "Parsing row, start: " << result->position();
      ParseRow(result, row);
    }
    result->PopBack();
  }

 private:
  void SkipSpaces(ParseData* result) {
    auto seek = result->seek();
    while (std::iswspace(seek.read()) && seek.Once() == Seek::Result::Done)
      continue;
  }

  void ParseRow(ParseData* result, size_t csv_column) {
    static const std::vector<Color> csv_column_colors = {
        Color::Cyan, Color::Yellow, Color::Green, Color::Blue, Color::Magenta};
    result->Push(CSV_CELL, ColumnNumberDelta(), {}, {});
    Style modifiers{
        .foreground_color =
            csv_column_colors[csv_column % csv_column_colors.size()]};
    auto seek = result->seek();
    SkipSpaces(result);
    switch (seek.read()) {
      case L'\"':
        seek.Once();
        ParseQuotedString(
            result, L'"', modifiers,
            ParseTree::PropertyMap{
                {ParseTreePropertyName::TableCell(csv_column), LazyString{}},
                {ParseTreePropertyName::StringValue(), LazyString{}}});
        break;
      default:
        if (isdigit(seek.read())) {
          seek.Once();
          ParseNumber(result, modifiers,
                      ParseTree::PropertyMap{
                          {ParseTreePropertyName::TableCell(csv_column),
                           LazyString{}}});
        } else {
          ColumnNumber start = result->position().column;
          seek.UntilCurrentCharNotIn({L','});
          result->PushAndPop(
              result->position().column - start, modifiers,
              ParseTree::PropertyMap{
                  {ParseTreePropertyName::TableCell(csv_column), LazyString{}},
                  {ParseTreePropertyName::NumberValue(), LazyString{}}});
        }
    }
    SkipSpaces(result);
    if (seek.read() == L',') {
      seek.Once();
      SkipSpaces(result);
      result->PushAndPop(ColumnNumberDelta(1),
                         Style{.attributes = StyleAttribute::Dim});
    }
    result->PopBack();  // CSV_CELL.
  }
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCsvTreeParser() {
  return NonNull<std::unique_ptr<CsvParser>>();
}
}  // namespace afc::editor::parsers
