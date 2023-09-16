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

enum State { DEFAULT, CSV_ROW };

class CsvParser : public TreeParser {
 public:
  ParseTree FindChildren(const LineSequence& buffer, Range range) override {
    std::vector<size_t> states_stack = {DEFAULT};
    std::vector<ParseTree> trees = {ParseTree(range)};

    range.ForEachLine([&](LineNumber i) {
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(i + LineNumberDelta(1)), range.end));
      data.set_position(std::max(LineColumn(i), range.begin));
      ParseLine(&data);
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, i);
      }
      states_stack = data.parse_results()->states_stack;
    });

    auto final_position =
        LineColumn(buffer.EndLine(), buffer.back()->EndColumn());
    if (final_position >= range.end) {
      DVLOG(5) << "Draining final states: " << states_stack.size();
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(LineNumber(0) + buffer.size() +
                                         LineNumberDelta(1)),
                              range.end));
      while (data.parse_results()->states_stack.size() > 1) {
        data.PopBack();
      }
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, final_position.line);
      }
    }
    CHECK(!trees.empty());
    return trees[0];
  }

  void ParseLine(ParseData* result) {
    SkipSpaces(result);
    result->Push(CSV_ROW, ColumnNumberDelta(), {}, {});
    while (result->seek().read() != L'\n') {
      VLOG(10) << "Parsing row, start: " << result->position();
      ParseRow(result);
    }
    result->PopBack();
  }

 private:
  void SkipSpaces(ParseData* result) {
    auto seek = result->seek();
    while (std::iswspace(seek.read()) && seek.Once() == Seek::DONE) continue;
  }

  void ParseRow(ParseData* result) {
    auto seek = result->seek();
    SkipSpaces(result);
    LineColumn start = result->position();
    switch (seek.read()) {
      case L'\"':
        seek.Once();
        ParseDoubleQuotedString(result, {LineModifier::kYellow});
        break;
      default:
        while (seek.read() != L',' && seek.Once() == Seek::DONE) continue;
    }
    SkipSpaces(result);
    if (seek.read() == L',') {
      seek.Once();
      result->PushAndPop(ColumnNumberDelta(1),
                         LineModifierSet{LineModifier::kCyan});
    }
  }
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCsvTreeParser() {
  return NonNull<std::unique_ptr<CsvParser>>();
}
}  // namespace afc::editor::parsers
