#ifndef __AFC_EDITOR_DIRECTION_H__
#define __AFC_EDITOR_DIRECTION_H__

#include <memory>

namespace afc {
namespace editor {

enum class Direction { kBackwards, kForwards };

Direction ReverseDirection(Direction direction);
std::wstring ToString(Direction direction);

}  // namespace editor
}  // namespace afc

#endif
