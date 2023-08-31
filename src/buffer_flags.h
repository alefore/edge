#ifndef __AFC_EDITOR_BUFFER_FLAGS_H__
#define __AFC_EDITOR_BUFFER_FLAGS_H__

#include <vector>

#include "src/buffer.h"
#include "src/infrastructure/screen/line_modifier.h"

namespace afc::editor {
std::vector<infrastructure::screen::LineModifier> GetBufferFlag(
    const OpenBuffer& buffer);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_FLAGS_H__
