#include "src/direction.h"

namespace afc {
namespace editor {

Direction ReverseDirection(Direction direction) {
  return direction == FORWARDS ? BACKWARDS : FORWARDS;
}

}  // namespace editor
}  // namespace afc
