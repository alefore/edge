#ifndef __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
#define __AFC_EDITOR_LINE_SCROLL_CONTROL_H__

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "src/line_output.h"
#include "src/line_with_cursor.h"
#include "src/widget.h"

// TODO(easy, 2022-04-30): Rename to 'buffer_contents_view_layout' or such.

namespace afc::editor {
struct BufferContentsWindow {
  struct Input {
    const BufferContents& contents;

    // If present, adjusts the view (`begin`) to contain this location.
    std::optional<LineColumn> active_position;

    const CursorsSet& active_cursors;

    LineWrapStyle line_wrap_style;
    std::wstring symbol_characters;

    // Maximum number of lines in the output. May return fewer lines (e.g., if
    // the contents are shorter).
    LineNumberDelta lines_shown;

    LineNumberDelta status_lines;

    // Total number of columns in the output for buffer contents.
    language::lazy_string::ColumnNumberDelta columns_shown;

    // Initial position in the buffer where output will begin.
    LineColumn begin;

    // Number of lines above the buffer->position() that should be shown.
    // Ignored if
    // - less than lines_shown / 2, or
    // - active_position is nullopt.
    LineNumberDelta margin_lines;
  };
  static BufferContentsWindow Get(Input input);

  struct Line {
    Range range;
    bool has_active_cursor;
    // Returns the set of cursors that fall in the current range.
    //
    // The column positions are relative to the beginning of the input line
    // (i.e., changing the range affects only whether a given cursor is
    // returned, but once the decision is made that a cursor will be returned,
    // the value returned for it won't be affected by the range).
    std::set<language::lazy_string::ColumnNumber> current_cursors;
  };
  std::vector<Line> lines;

  // Update information on the buffer: set the requested start at this position.
  // This may not match the beginning of `lines` because the status may have
  // obstructed part of the view.
  LineColumn view_start;
};
}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::BufferContentsWindow::Line> {
  std::size_t operator()(
      const afc::editor::BufferContentsWindow::Line& line) const;
};

template <>
struct hash<afc::editor::BufferContentsWindow> {
  std::size_t operator()(const afc::editor::BufferContentsWindow& window) const;
};
}  // namespace std
#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
