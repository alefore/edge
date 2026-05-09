#ifndef __AFC_EDITOR_SRC_LINE_MARKS_SCANNER_H__
#define __AFC_EDITOR_SRC_LINE_MARKS_SCANNER_H__
#include "src/buffer_name.h"
#include "src/editor.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
void ScanLineMarks(EditorState& editor, BufferName buffer_name,
                   language::text::LineSequence contents,
                   language::text::LineNumberDelta start_new_section,
                   language::text::LineNumberDelta lines_added);
}
#endif