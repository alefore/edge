#include "src/direction.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

Direction ReverseDirection(Direction direction) {
  switch (direction) {
    case Direction::Forwards:
      return Direction::Backwards;
    case Direction::Backwards:
      return Direction::Forwards;
  }
  LOG(FATAL) << "Invalid direction value.";
  return Direction::Forwards;
}

std::wstring ToString(Direction direction) {
  switch (direction) {
    case Direction::Forwards:
      return L"Forwards";
    case Direction::Backwards:
      return L"Backwards";
  }
  LOG(FATAL) << "Invalid direction.";
  return L"Invalid direction.";
}

}  // namespace editor
}  // namespace afc
