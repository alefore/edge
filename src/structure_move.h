#ifndef __AFC_VM_STRUCTURE_MOVE_H__
#define __AFC_VM_STRUCTURE_MOVE_H__

#include <optional>

#include "src/structure.h"

namespace afc::editor {

class OperationScopeBufferInformation;
class BufferContents;
class LineColumn;
class Range;
class Modifiers;

std::optional<LineColumn> Move(const OperationScopeBufferInformation& scope,
                               Structure structure,
                               const BufferContents& contents,
                               LineColumn position, Range range,
                               const Modifiers& modifiers);

}  // namespace afc::editor

#endif  // __AFC_VM_STRUCTURE_MOVE_H__
