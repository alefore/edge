#include "src/parsers/diff.h"

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

enum State { DEFAULT, HEADERS, SECTION, CONTENTS, FILE_LINE };

class DiffParser : public LineOrientedTreeParser {
 protected:
  void ParseLine(ParseData* result) override {
    switch (result->seek().read()) {
      case L'\n':
      case L' ':
        InContents(result, {});
        return;

      case L'+':
        if (result->state() == HEADERS || result->state() == DEFAULT) {
          if (!HandlePath(result))
            AdvanceLine(result, {LineModifier::kBold, LineModifier::kGreen});
          return;
        }
        // Fall through.
      case L'>':
        InContents(result, {LineModifier::kGreen});
        return;

      case L'-':
        if (result->state() == HEADERS || result->state() == DEFAULT) {
          if (!HandlePath(result))
            AdvanceLine(result, {LineModifier::kBold, LineModifier::kRed});
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
  bool HandlePath(ParseData* result) {
    auto seek = result->seek();

    wchar_t c = seek.read();
    for (int i = 0; i < 3; i++)
      if (seek.read() != c || seek.Once() == Seek::UNABLE_TO_ADVANCE)
        return false;

    if (seek.read() != ' ' || seek.Once() == Seek::UNABLE_TO_ADVANCE)
      return false;

    if (seek.read() == '/' && seek.Once() == Seek::UNABLE_TO_ADVANCE)
      return false;

    while (seek.read() != '/' && seek.Once() == Seek::DONE)
      ; /* Nothing. */

    if (seek.Once() == Seek::UNABLE_TO_ADVANCE) return false;

    ColumnNumber path_start = result->position().column;
    VLOG(7) << "Found link starting at: " << path_start;
    result->Push(FILE_LINE, path_start.ToDelta(),
                 {LineModifier::kBold,
                  c == '+' ? LineModifier::kGreen : LineModifier::kRed},
                 {ParseTreeProperty::Link()});
    seek.ToEndOfLine();
    result->PushAndPop(result->position().column - path_start,
                       {LineModifier::kUnderline},
                       {ParseTreeProperty::LinkTarget()});
    result->PopBack();
    return true;
  }

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
