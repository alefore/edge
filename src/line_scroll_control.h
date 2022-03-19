#ifndef __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
#define __AFC_EDITOR_LINE_SCROLL_CONTROL_H__

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "src/line_output.h"
#include "src/output_producer.h"
#include "src/widget.h"

namespace afc::editor {
struct ComputeScreenLinesInput {
  std::shared_ptr<BufferContents> contents;

  // If present, adjusts the view (`begin`) to contain this location.
  std::optional<LineColumn> active_position;

  CursorsSet* active_cursors;

  LineWrapStyle line_wrap_style;
  std::wstring symbol_characters;

  // Total number of lines in the output.
  LineNumberDelta lines_shown;

  // Total number of columns in the output for buffer contents.
  ColumnNumberDelta columns_shown;

  // Initial position in the buffer where output will begin.
  LineColumn begin;

  // Number of lines above the buffer->position() that should be shown.
  // Ignored if
  // - less than lines_shown / 2, or
  // - active_position is nullopt.
  LineNumberDelta margin_lines;
};

struct ScreenLine {
  Range range;
  bool has_active_cursor;
  // Returns the set of cursors that fall in the current range.
  //
  // The column positions are relative to the beginning of the input line
  // (i.e., changing the range affects only whether a given cursor is
  // returned, but once the decision is made that a cursor will be returned,
  // the value returned for it won't be affected by the range).
  std::set<ColumnNumber> current_cursors;
};

std::list<ScreenLine> ComputeScreenLines(ComputeScreenLinesInput input);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
