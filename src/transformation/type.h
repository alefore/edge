#ifndef __AFC_EDITOR_TRANSFORMATION_TYPE_H__
#define __AFC_EDITOR_TRANSFORMATION_TYPE_H__

#include <optional>
#include <variant>

#include "src/transformation.h"
#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/set_position.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class CompositeTransformation;
namespace transformation {
class Stack;
class SwapActiveCursor;
class ModifiersAndComposite;
using CompositePtr = std::shared_ptr<editor::CompositeTransformation>;

using BaseTransformation =
    std::variant<Delete, ModifiersAndComposite, CompositePtr, Cursors, Insert,
                 Repetitions, SetPosition, Stack, SwapActiveCursor>;
}  // namespace transformation
}  // namespace afc::editor

// Can't be included before we define BaseTransformations, since it needs it.
#include "src/transformation/composite.h"
#include "src/transformation/stack.h"

namespace afc::editor::transformation {
void Register(vm::Environment* environment);

void BaseTransformationRegister(vm::Environment* environment);

std::unique_ptr<Transformation> Build(BaseTransformation base_transformation);
}  // namespace afc::editor::transformation
#endif
