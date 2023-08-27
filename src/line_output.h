// Helper functions for Line class.

#ifndef __AFC_EDITOR_LINE_OUTPUT_H__
#define __AFC_EDITOR_LINE_OUTPUT_H__

#include "src/language/text/line.h"

namespace afc::editor {
enum class LineWrapStyle { kBreakWords, kContentBased };
struct ColumnRange {
  language::lazy_string::ColumnNumber begin;
  language::lazy_string::ColumnNumber end;

  bool operator==(const ColumnRange& other) const {
    return begin == other.begin && end == other.end;
  }
};

// Breaks `line` into separate ranges to be printed without overflowing a
// desired screein width, taking into account double-width characters.
std::list<ColumnRange> BreakLineForOutput(
    const language::text::Line& line,
    language::lazy_string::ColumnNumberDelta screen_positions,
    LineWrapStyle line_wrap_style, std::wstring symbol_characters);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_OUTPUT_H__
