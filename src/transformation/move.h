#ifndef __AFC_EDITOR_TRANSFORMATION_MOVE_H__
#define __AFC_EDITOR_TRANSFORMATION_MOVE_H__

#include <memory>
#include <string>

#include "src/language/safe_types.h"
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
SwapActiveCursor OptimizeBase(SwapActiveCursor transformation);
}  // namespace transformation

class OperationScope;
class CompositeTransformation;
language::NonNull<std::unique_ptr<CompositeTransformation>>
NewMoveTransformation();
language::NonNull<std::unique_ptr<CompositeTransformation>>
NewMoveTransformation(
    language::NonNull<std::shared_ptr<OperationScope>> operation_scope);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_MOVE_H__
