#include "src/buffer_name.h"

#include "src/language/overload.h"
#include "src/language/wstring.h"

using afc::language::overload;

namespace afc::editor {

using ::operator<<;

std::wstring to_wstring(const BufferName& p) {
  return std::visit(
      overload{
          [&](const BufferFileId& i) { return to_wstring(i); },
          [&](const BufferListId&) -> std::wstring { return L"- buffers"; },
          [&](const PasteBuffer&) -> std::wstring { return L"- paste buffer"; },
          [&](const TextInsertion&) -> std::wstring {
            return L"- text inserted";
          },
          [&](const CommandBufferName& input) -> std::wstring {
            return L"$ " + to_wstring(input);
          },
          [&](const std::wstring& str) { return str; },
      },
      p);
}
std::ostream& operator<<(std::ostream& os, const BufferName& p) {
  os << to_wstring(p);
  return os;
}

}  // namespace afc::editor
