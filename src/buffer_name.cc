#include "src/buffer_name.h"

namespace afc::editor {

/* static */ const BufferName& BufferName::BuffersList() {
  static const BufferName* const value = new BufferName(L"- buffers");
  return *value;
}

/* static */ const BufferName& BufferName::PasteBuffer() {
  static const BufferName* const value = new BufferName(L"- paste buffer");
  return *value;
}

/* static */ const BufferName& BufferName::TextInsertion() {
  static const BufferName* const value = new BufferName(L"- text inserted");
  return *value;
}

BufferName::BufferName(Path path) : value(path.ToString()) {}

const std::wstring& BufferName::read() const { return value; }

}  // namespace afc::editor
