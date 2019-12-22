#ifndef __AFC_EDITOR_TRANSFORMATION_GOTO_POSITION_H__
#define __AFC_EDITOR_TRANSFORMATION_GOTO_POSITION_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
std::unique_ptr<Transformation> NewGotoPositionTransformation(
    LineColumn position);
// If column is greater than the length of the line, goes to the end of the
// line.
std::unique_ptr<Transformation> NewGotoPositionTransformation(
    std::optional<LineNumber>, ColumnNumber position);
void RegisterGotoPositionTransformation(vm::Environment* environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_GOTO_POSITION_H__
