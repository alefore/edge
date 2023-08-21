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
#include "src/language/lazy_string/padding.h"
#include "src/language/wstring.h"
#include "src/widget.h"

namespace afc {
namespace editor {
using language::CaptureAndHash;
using language::HashableContainer;
using language::MakeNonNullShared;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

/* static */ ColumnNumberDelta LineNumberOutputWidth(
    LineNumberDelta lines_size) {
  static const ColumnNumberDelta kColon(1);
  // We don't subtract LineNumberDelta(1): humans start counting from 1.
  return kColon + ColumnNumberDelta(to_wstring(lines_size).size());
}

LineModifierSet LineModifiers(const BufferContentsViewLayout::Line& line,
                              const OpenBuffer& buffer) {
  if (line.current_cursors.empty()) {
    return {LineModifier::kDim};
  } else if (line.has_active_cursor ||
             buffer.Read(buffer_variables::multiple_cursors)) {
    return {LineModifier::kCyan, LineModifier::kBold};
  } else {
    return {LineModifier::kBlue};
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
    if (screen_line.range.begin.line > buffer.EndLine()) {
      return output;  // The buffer is smaller than the screen.
    }

    output.lines.push_back(LineWithCursor::Generator::New(CaptureAndHash(
        [](Range range, ColumnNumberDelta width,
           HashableContainer<LineModifierSet> modifiers) {
          std::wstring number =
              range.begin.column.IsZero()
                  ? to_wstring(range.begin.line + LineNumberDelta(1))
                  : L"↪";
          CHECK_LE(ColumnNumberDelta(number.size() + 1), width);
          language::NonNull<std::shared_ptr<LazyString>> padding =
              Padding(width - ColumnNumberDelta(number.size() + 1), L' ');

          Line::Options line_options;
          line_options.AppendString(
              Append(padding, NewLazyString(number + L":")),
              modifiers.container);
          return LineWithCursor{
              .line = MakeNonNullShared<Line>(std::move(line_options))};
        },
        screen_line.range, output.width,
        HashableContainer<LineModifierSet>(
            LineModifiers(screen_line, buffer)))));
  }
  return output;
}

}  // namespace editor
}  // namespace afc
