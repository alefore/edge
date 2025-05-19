#ifndef __AFC_EDITOR_TRANSFORMATION_VARIANT_H__
#define __AFC_EDITOR_TRANSFORMATION_VARIANT_H__
 
#include <optional>
#include <variant>

#include "src/transformation/cursors.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/vm/environment.h"

namespace afc::editor {
class OpenBuffer;
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
