#ifndef __AFC_EDITOR_BUFFER_FLAG_MAP_H__
#define __AFC_EDITOR_BUFFER_FLAG_MAP_H__

#include <map>

#include "src/language/lazy_string/single_line.h"

namespace afc::editor {
struct BufferFlagKey
    : public language::GhostType<BufferFlagKey,
                                 language::lazy_string::SingleLine> {
  using GhostType::GhostType;
};

struct BufferFlagValue
    : public language::GhostType<BufferFlagValue,
                                 language::lazy_string::SingleLine> {
 public:
  using GhostType::GhostType;
  // Convenience constructor.
  BufferFlagValue(language::lazy_string::NonEmptySingleLine input)
      : BufferFlagValue(input.read()) {}
};

using BufferFlagMap = std::map<BufferFlagKey, BufferFlagValue>;
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_FLAG_MAP_H__
