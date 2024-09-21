#ifndef __AFC_EDITOR_PATH_FLAGS_H__
#define __AFC_EDITOR_PATH_FLAGS_H__

#include <map>
#include <vector>

#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/wstring.h"

namespace afc::editor::flags {

struct InputKey
    : public language::GhostType<InputKey,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;
};
struct InputValue
    : public language::GhostType<InputValue,
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;
};
struct Color
    : public language::GhostType<Color,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;
};

std::vector<Color> GenerateFlags(const std::vector<InputKey>& spec,
                                 const std::vector<Color>& colors,
                                 std::map<InputKey, InputValue> inputs);

}  // namespace afc::editor::flags

#endif  // __AFC_EDITOR_PATH_FLAGS_H__
