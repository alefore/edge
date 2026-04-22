#ifndef __AFC_EDITOR_TRANSFORMATION_VARIANT_H__
#define __AFC_EDITOR_TRANSFORMATION_VARIANT_H__

#include <optional>
#include <variant>

#include "src/transformation_cursors.h"
#include "src/transformation_delete.h"
#include "src/transformation_insert.h"
#include "src/transformation_move.h"
#include "src/transformation_set_position.h"
#include "src/vm/environment.h"

namespace afc::editor {
class CompositeTransformation;
namespace transformation {
using CompositePtr =
    language::NonNull<std::shared_ptr<editor::CompositeTransformation>>;

struct ModifiersAndComposite;
struct Repetitions;
struct Stack;
struct SwapActiveCursor;
struct VisualOverlay;

using Variant = std::variant<Delete, ModifiersAndComposite, CompositePtr,
                             Cursors, Insert, Repetitions, SetPosition, Stack,
                             SwapActiveCursor, VisualOverlay>;
}  // namespace transformation
}  // namespace afc::editor
#endif
