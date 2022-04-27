#ifndef __AFC_EDITOR_SCREEN_CURSES_H__
#define __AFC_EDITOR_SCREEN_CURSES_H__

#include <memory>

#include "screen.h"
#include "src/language/safe_types.h"

namespace afc::editor {

wint_t ReadChar(std::mbstate_t* mbstate);

language::NonNull<std::unique_ptr<Screen>> NewScreenCurses();

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SCREEN_CURSES_H__
