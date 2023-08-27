#ifndef __AFC_EDITOR_OPERATION_SCOPE_BUFFER_INFORMATION_H__
#define __AFC_EDITOR_OPERATION_SCOPE_BUFFER_INFORMATION_H__

#include "src/language/text/line_column.h"
#include "src/line_marks.h"

namespace afc::editor {
// Freezes information about buffers in the scope of an operation. This makes
// the operation repeatable: if the information changes in the buffers, those
// changes won't affect repeated applications of the operation.
//
// This is used for PageUp/PageDown. If the screen sizes, we still scroll by the
// original screen size.
struct OperationScopeBufferInformation {
  language::text::LineNumberDelta screen_lines;

  std::multimap<language::text::LineColumn, LineMarks::Mark> line_marks;

  // From buffer_variables::margin_lines_ratio.
  double margin_lines_ratio;
};
}  // namespace afc::editor
#endif  //__AFC_EDITOR_OPERATION_SCOPE_BUFFER_INFORMATION_H__
