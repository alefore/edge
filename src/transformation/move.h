#ifndef __AFC_EDITOR_TRANSFORMATION_MOVE_H__
#define __AFC_EDITOR_TRANSFORMATION_MOVE_H__

#include <memory>
#include <string>

#include "src/modifiers.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"

namespace afc::editor {
namespace transformation {
// Transformation that swaps the current cursor with the next active cursor.
struct SwapActiveCursor {
  // Honors `direction` and `repetitions`. May honor more modifiers in the
  // future.
  Modifiers modifiers;
};

futures::Value<Result> ApplyBase(const SwapActiveCursor& parameters,
                                 Input input);
std::wstring ToStringBase(const SwapActiveCursor& v);
}  // namespace transformation

class CompositeTransformation;
std::unique_ptr<CompositeTransformation> NewMoveTransformation();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_MOVE_H__
