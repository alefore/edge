#ifndef __AFC_VM_STRUCTURE_MOVE_H__
#define __AFC_VM_STRUCTURE_MOVE_H__

#include <optional>

#include "src/language/text/line_column.h"
#include "src/structure.h"

namespace afc::editor {

class OperationScopeBufferInformation;
class BufferContents;
class Modifiers;

std::optional<language::text::LineColumn> Move(
    const OperationScopeBufferInformation& scope, Structure structure,
    const BufferContents& contents, language::text::LineColumn position,
    language::text::Range range, const Modifiers& modifiers);

}  // namespace afc::editor

#endif  // __AFC_VM_STRUCTURE_MOVE_H__
