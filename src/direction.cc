#include "src/direction.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

Direction ReverseDirection(Direction direction) {
  switch (direction) {
    case Direction::kForwards:
      return Direction::kBackwards;
    case Direction::kBackwards:
      return Direction::kForwards;
  }
  LOG(FATAL) << "Invalid direction value.";
  return Direction::kForwards;
}

std::wstring ToString(Direction direction) {
  switch (direction) {
    case Direction::kForwards:
      return L"Forwards";
    case Direction::kBackwards:
      return L"Backwards";
  }
  LOG(FATAL) << "Invalid direction.";
  return L"Invalid direction.";
}

}  // namespace editor
}  // namespace afc
