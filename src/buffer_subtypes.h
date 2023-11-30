#ifndef __AFC_EDITOR_BUFFER_SUBTYPES__
#define __AFC_EDITOR_BUFFER_SUBTYPES__

#include <variant>

namespace afc::editor {
class OpenBuffer;

class OpenBufferPasteMode;
class OpenBufferNoPasteMode;

std::variant<OpenBufferPasteMode, OpenBufferNoPasteMode> GetPasteModeVariant(
    OpenBuffer& buffer);

struct OpenBufferPasteMode {
  OpenBuffer& value;

 private:
  OpenBufferPasteMode(OpenBuffer&);

  friend std::variant<OpenBufferPasteMode, OpenBufferNoPasteMode>
  GetPasteModeVariant(OpenBuffer& buffer);
};

struct OpenBufferNoPasteMode {
  OpenBuffer& value;

 private:
  OpenBufferNoPasteMode(OpenBuffer&);

  friend std::variant<OpenBufferPasteMode, OpenBufferNoPasteMode>
  GetPasteModeVariant(OpenBuffer& buffer);
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_SUBTYPES__
