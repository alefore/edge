#include <memory>

#include "modifiers.h"
#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewMoveTransformation(const Modifiers& modifiers);

}  // namespace editor
}  // namespace afc
