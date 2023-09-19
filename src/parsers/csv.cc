#include "src/parsers/csv.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/language/text/line_sequence.h"
#include "src/parse_tools.h"
#include "src/parsers/util.h"
#include "src/seek.h"

namespace afc::editor::parsers {
namespace {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::Range;

enum State { DEFAULT, CSV_ROW, CSV_CELL };

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
    while (std::iswspace(seek.read()) && seek.Once() == Seek::DONE) continue;
  }

  void ParseRow(ParseData* result, size_t csv_column) {
    static const std::vector<LineModifier> csv_column_colors = {
        LineModifier::kCyan, LineModifier::kYellow, LineModifier::kGreen,
        LineModifier::kBlue, LineModifier::kMagenta};
    result->Push(CSV_CELL, ColumnNumberDelta(), {}, {});
    LineModifierSet modifiers = {
        csv_column_colors[csv_column % csv_column_colors.size()]};
    auto seek = result->seek();
    SkipSpaces(result);
    switch (seek.read()) {
      case L'\"':
        seek.Once();
        ParseDoubleQuotedString(result, modifiers,
                                {ParseTreeProperty::TableCell(csv_column),
                                 ParseTreeProperty::StringValue()});
        break;
      default:
        if (isdigit(seek.read())) {
          seek.Once();
          ParseNumber(result, modifiers,
                      {ParseTreeProperty::TableCell(csv_column)});
        } else {
          ColumnNumber start = result->position().column;
          while (seek.read() != L',' && seek.Once() == Seek::DONE) continue;
          result->PushAndPop(result->position().column - start, modifiers,
                             {ParseTreeProperty::TableCell(csv_column),
                              ParseTreeProperty::NumberValue()});
        }
    }
    SkipSpaces(result);
    if (seek.read() == L',') {
      seek.Once();
      SkipSpaces(result);
      result->PushAndPop(ColumnNumberDelta(1),
                         LineModifierSet{LineModifier::kDim});
    }
    result->PopBack();  // CSV_CELL.
  }
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCsvTreeParser() {
  return NonNull<std::unique_ptr<CsvParser>>();
}
}  // namespace afc::editor::parsers
