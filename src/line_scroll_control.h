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

namespace afc {
namespace editor {
class LineScrollControl {
 private:
  struct ConstructorAccessTag {};

 public:
  struct Options {
    std::shared_ptr<BufferContents> contents;

    LineColumn active_position;
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
    // Ignored if less than lines_shown / 2, ignored.
    LineNumberDelta margin_lines;
  };

  static std::shared_ptr<LineScrollControl> New(
      LineScrollControl::Options options) {
    return std::make_shared<LineScrollControl>(ConstructorAccessTag(),
                                               std::move(options));
  }

  LineScrollControl(ConstructorAccessTag, Options options);

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
  std::list<ScreenLine> screen_lines() const;

 private:
  ScreenLine GetScreenLine(LineNumber line, ColumnRange range) const;
  std::list<ColumnRange> ComputeBreaks(LineNumber line) const;
  std::list<ScreenLine> PrependLines(LineNumber line,
                                     LineNumberDelta lines_desired,
                                     std::list<ScreenLine> output) const;
  std::list<ScreenLine> AdjustToHonorMargin(std::list<ScreenLine> output) const;
  std::list<ScreenLine> ComputeScreenLines() const;

  const Options options_;
  const std::map<LineNumber, std::set<ColumnNumber>> cursors_;

  // Contains one element for each (screen) line to show, with the corresponding
  // range.
  const std::list<ScreenLine> screen_lines_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
