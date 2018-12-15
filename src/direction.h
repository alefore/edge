#ifndef __AFC_EDITOR_DIRECTION_H__
#define __AFC_EDITOR_DIRECTION_H__

#include <memory>

namespace afc {
namespace editor {

enum Direction { BACKWARDS, FORWARDS };

Direction ReverseDirection(Direction direction);

}  // namespace editor
}  // namespace afc

#endif
