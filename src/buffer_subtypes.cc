#include "src/buffer_subtypes.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"

namespace afc::editor {
std::variant<OpenBufferPasteMode, OpenBufferNoPasteMode> GetPasteModeVariant(
    OpenBuffer& buffer) {
  if (buffer.Read(buffer_variables::paste_mode))
    return OpenBufferPasteMode{buffer};
  else
    return OpenBufferNoPasteMode{buffer};
}

OpenBufferPasteMode::OpenBufferPasteMode(OpenBuffer& buffer) : value(buffer) {}

OpenBufferNoPasteMode::OpenBufferNoPasteMode(OpenBuffer& buffer)
    : value(buffer) {}

}  // namespace afc::editor
