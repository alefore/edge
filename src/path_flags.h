#ifndef __AFC_EDITOR_PATH_FLAGS_H__
#define __AFC_EDITOR_PATH_FLAGS_H__

#include <map>
#include <string>
#include <vector>

#include "src/language/ghost_type_class.h"
#include "src/language/wstring.h"

namespace afc::editor::flags {

struct InputKey : public language::GhostType<InputKey, std::wstring> {};
struct InputValue : public language::GhostType<InputValue, std::wstring> {};
struct Color : public language::GhostType<Color, std::wstring> {};

std::vector<Color> GenerateFlags(const std::vector<InputKey>& spec,
                                 const std::vector<Color>& colors,
                                 std::map<InputKey, InputValue> inputs);

}  // namespace afc::editor::flags

#endif  // __AFC_EDITOR_PATH_FLAGS_H__
