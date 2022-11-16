#ifndef __AFC_EDITOR_PATH_FLAGS_H__
#define __AFC_EDITOR_PATH_FLAGS_H__

#include <map>
#include <string>
#include <vector>

#include "src/language/ghost_type.h"
#include "src/language/wstring.h"

namespace afc::editor::flags {

GHOST_TYPE(InputKey, std::wstring);
GHOST_TYPE(InputValue, std::wstring);
GHOST_TYPE(Color, std::wstring);

std::vector<Color> GenerateFlags(const std::vector<InputKey>& spec,
                                 const std::vector<Color>& colors,
                                 std::map<InputKey, InputValue> inputs);

}  // namespace afc::editor::flags

GHOST_TYPE_TOP_LEVEL(afc::editor::flags::InputValue);

#endif  // __AFC_EDITOR_PATH_FLAGS_H__
