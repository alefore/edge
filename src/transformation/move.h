#ifndef __AFC_EDITOR_TRANSFORMATION_MOVE_H__
#define __AFC_EDITOR_TRANSFORMATION_MOVE_H__

#include <memory>

#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace transformation {
// Transformation that swaps the current cursor with the next active cursor.
struct SwapActiveCursor {};

futures::Value<Transformation::Result> ApplyBase(
    const SwapActiveCursor& parameters, Transformation::Input input);
}  // namespace transformation

class CompositeTransformation;
std::unique_ptr<CompositeTransformation> NewMoveTransformation();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_MOVE_H__
