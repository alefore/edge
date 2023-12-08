#ifndef __AFC_EDITOR_LINE_MARKS_BUFFER_H__
#define __AFC_EDITOR_LINE_MARKS_BUFFER_H__

#include "src/command.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"

namespace afc::editor {
language::gc::Root<Command> NewLineMarksBufferCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_MARKS_BUFFER_H__
