#ifndef __AFC_EDITOR_TRANSFORMATION_TYPE_H__
#define __AFC_EDITOR_TRANSFORMATION_TYPE_H__

#include <optional>
#include <variant>

#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class OpenBuffer;
class CompositeTransformation;
namespace transformation {
using CompositePtr = std::shared_ptr<editor::CompositeTransformation>;

class ModifiersAndComposite;
class Repetitions;
class Stack;
class SwapActiveCursor;

using Variant =
    std::variant<Delete, ModifiersAndComposite, CompositePtr, Cursors, Insert,
                 Repetitions, SetPosition, Stack, SwapActiveCursor>;
}  // namespace transformation
}  // namespace afc::editor

// Can't be included before we define Variant, since it needs it.
#include "src/transformation/composite.h"
#include "src/transformation/repetitions.h"
#include "src/transformation/stack.h"

namespace afc::editor::transformation {
void Register(vm::Environment* environment);

void BaseTransformationRegister(vm::Environment* environment);

class Result;
class Input;
futures::Value<Result> Apply(Variant base_transformation, const Input& input);

std::wstring ToString(const Variant& transformation);

}  // namespace afc::editor::transformation
#endif
