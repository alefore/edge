#include "src/line_number_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_contents_view_layout.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/editor_variables.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
#include "src/widget.h"

using afc::infrastructure::screen::Color;using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::CaptureAndHash;
using afc::language::HashableContainer;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumberDelta;
using afc::language::text::LinePartMetadata;
using afc::language::text::LineRange;
using afc::language::text::Range;

namespace afc {
namespace editor {

/* static */ ColumnNumberDelta LineNumberOutputWidth(
    LineNumberDelta lines_size) {
  static const ColumnNumberDelta kColon(1);
  // We don't subtract LineNumberDelta(1): humans start counting from 1.
  return kColon + ColumnNumberDelta(to_wstring(lines_size).size());
}

Style LineNumberStyle(const BufferContentsViewLayout::Line& line,
                      const OpenBuffer& buffer) {
  if (line.current_cursors.empty()) {
    return Style{.attributes = StyleAttribute::Dim};
  } else if (line.has_active_cursor ||
             buffer.Read(buffer_variables::multiple_cursors)) {
    return Style{.foreground_color = StandardColor::Cyan,
                 .attributes = StyleAttribute::Bold};
  } else {
    return Style{.foreground_color = StandardColor::Blue};
  }
}

LineWithCursor::Generator::Vector LineNumberOutput(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsViewLayout::Line>& screen_lines) {
  LineWithCursor::Generator::Vector output{
      .lines = {},
      .width = std::max(LineNumberOutputWidth(buffer.lines_size()),
                        ColumnNumberDelta(buffer.editor().Read(
                            editor_variables::numbers_column_padding)))};
  for (const BufferContentsViewLayout::Line& screen_line : screen_lines) {
    if (screen_line.range.line() > buffer.EndLine()) {
      return output;  // The buffer is smaller than the screen.
    }

    output.lines.push_back(LineWithCursor::Generator::New(CaptureAndHash(
        [](LineRange range, ColumnNumberDelta width, Style style) {
          SingleLine number = range.begin_column().IsZero()
                                  ? SingleLine{LazyString{to_wstring(
                                        range.line() + LineNumberDelta(1))}}
                                  : SingleLine::Char<L'↪'>();
          CHECK_LE(ColumnNumberDelta(number.size() + 1), width);
          SingleLine padding =
              SingleLine::Padding(width - ColumnNumberDelta(number.size() + 1));
          LineBuilder line_options;
          line_options.AppendString(padding + number + SingleLine::Char<L':'>(),
                                    LinePartMetadata{.style = style});
          return LineWithCursor{.line = std::move(line_options).Build()};
        },
        screen_line.range, output.width,
        LineNumberStyle(screen_line, buffer))));
  }
  return output;
}

}  // namespace editor
}  // namespace afc
