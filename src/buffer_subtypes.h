#ifndef __AFC_EDITOR_BUFFER_SUBTYPES__
#define __AFC_EDITOR_BUFFER_SUBTYPES__

#include <variant>

namespace afc::editor {
class OpenBuffer;

struct OpenBufferPasteMode {
  OpenBuffer& buffer;
};

struct OpenBufferNoPasteMode {
  OpenBuffer& buffer;
};

std::variant<OpenBufferPasteMode, OpenBufferNoPasteMode> GetPasteModeVariant(
    OpenBuffer& buffer);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_SUBTYPES__
