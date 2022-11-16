#ifndef __AFC_EDITOR_BUFFER_FLAGS_H__
#define __AFC_EDITOR_BUFFER_FLAGS_H__

#include <map>
#include <string>
#include <vector>

#include "src/language/ghost_type.h"
#include "src/language/wstring.h"
#include "src/line_with_cursor.h"

namespace afc::editor {
std::vector<LineModifier> GetBufferFlag(const OpenBuffer& buffer);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_FLAGS_H__
