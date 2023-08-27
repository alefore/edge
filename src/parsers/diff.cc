#include "src/parsers/diff.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer_contents.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc::editor::parsers {
namespace {
using language::NonNull;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::Range;

enum State { DEFAULT, HEADERS, SECTION, CONTENTS };

class DiffParser : public TreeParser {
 public:
  ParseTree FindChildren(const BufferContents& buffer, Range range) override {
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
    switch (result->seek().read()) {
      case L'\n':
      case L' ':
        InContents(result, {});
        return;

      case L'+':
        if (result->state() == HEADERS) {
          AdvanceLine(result, {LineModifier::kBold});
          return;
        }
        // Fall through.
      case L'>':
        InContents(result, {LineModifier::kGreen});
        return;

      case L'-':
        if (result->state() == HEADERS) {
          AdvanceLine(result, {LineModifier::kBold});
          return;
        }
        // Fall through.
      case L'<':
        InContents(result, {LineModifier::kRed});
        return;

      case L'@':
        if (result->state() == CONTENTS) {
          result->PopBack();
        }
        if (result->state() == SECTION) {
          result->PopBack();
        }
        result->Push(SECTION, ColumnNumberDelta(), {}, {});
        AdvanceLine(result, {LineModifier::kCyan});
        return;

      default:
        if (result->state() != HEADERS) {
          if (result->state() == CONTENTS) {
            result->PopBack();
          }
          if (result->state() == SECTION) {
            result->PopBack();
          }
          if (result->state() == HEADERS) {
            result->PopBack();
          }
          result->Push(HEADERS, ColumnNumberDelta(), {}, {});
        }
        AdvanceLine(result, {LineModifier::kBold});
        return;
    }
  }

 private:
  void AdvanceLine(ParseData* result, LineModifierSet modifiers) {
    auto original_column = result->position().column;
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column - original_column,
                       {modifiers});
  }

  void InContents(ParseData* result, LineModifierSet modifiers) {
    if (result->state() != CONTENTS) {
      result->Push(CONTENTS, ColumnNumberDelta(), {}, {});
    }
    AdvanceLine(result, modifiers);
  }
};

}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewDiffTreeParser() {
  return NonNull<std::unique_ptr<DiffParser>>();
}
}  // namespace afc::editor::parsers
