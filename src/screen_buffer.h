#ifndef __AFC_EDITOR_SCREEN_BUFFER_H__
#define __AFC_EDITOR_SCREEN_BUFFER_H__

#include <memory>

#include "screen.h"

namespace afc {
namespace editor {

std::unique_ptr<Screen> NewScreenBuffer(std::shared_ptr<Screen> delegate);

}  // namespace afc
}  // namespace editor

#endif  // __AFC_EDITOR_SCREEN_BUFFER_H__
