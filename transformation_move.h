#include <memory>

#include "transformation.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<Transformation> NewMoveTransformation();

}  // namespace editor
}  // namespace afc
