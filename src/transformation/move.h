#include <memory>

#include "src/modifiers.h"
#include "src/transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewMoveTransformation(const Modifiers& modifiers);

}  // namespace editor
}  // namespace afc
