#ifndef __AFC_EDITOR_SCREEN_CURSES_H__
#define __AFC_EDITOR_SCREEN_CURSES_H__

#include <memory>

#include "screen.h"

namespace afc {
namespace editor {

wint_t ReadChar(std::mbstate_t* mbstate);

std::unique_ptr<Screen> NewScreenCurses();

}  // namespace afc
}  // namespace editor

#endif  // __AFC_EDITOR_SCREEN_CURSES_H__
