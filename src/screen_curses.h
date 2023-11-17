#ifndef __AFC_EDITOR_SCREEN_CURSES_H__
#define __AFC_EDITOR_SCREEN_CURSES_H__

#include <memory>

#include "src/infrastructure/extended_char.h"
#include "src/infrastructure/screen/screen.h"
#include "src/language/safe_types.h"

namespace afc::editor {

std::optional<infrastructure::ExtendedChar> ReadChar(std::mbstate_t* mbstate);

language::NonNull<std::unique_ptr<infrastructure::screen::Screen>>
NewScreenCurses();

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SCREEN_CURSES_H__
