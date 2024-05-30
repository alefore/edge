#include "src/buffer_name.h"

#include "src/language/overload.h"
#include "src/language/wstring.h"

using afc::language::overload;

namespace afc::editor {

using ::operator<<;

std::wstring to_wstring(const BufferName& p) {
  return std::visit(
      overload{
          [&](BufferFileId i) { return to_wstring(i); },
          [&](BufferListId) -> std::wstring { return L"- buffers"; },
          [&](PasteBuffer) -> std::wstring { return L"- paste buffer"; },
          [&](TextInsertion) -> std::wstring { return L"- text inserted"; },
          [&](std::wstring str) { return str; },
      },
      p);
}
std::ostream& operator<<(std::ostream& os, const BufferName& p) {
  os << to_wstring(p);
  return os;
}

}  // namespace afc::editor
