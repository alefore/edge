#include "src/buffer_name.h"

#include "src/language/overload.h"
#include "src/language/wstring.h"

using afc::language::overload;

namespace afc::editor {

using ::operator<<;

std::wstring to_wstring(const BufferName& p) {
  return std::visit(
      overload{
          [](const BufferFileId& i) { return to_wstring(i); },
          [](const BufferListId&) -> std::wstring { return L"- buffers"; },
          [](const PasteBuffer&) -> std::wstring { return L"- paste buffer"; },
          [](const TextInsertion&) -> std::wstring {
            return L"- text inserted";
          },
          [](const InitialCommands&) -> std::wstring {
            return L"- initial commands";
          },
          [](const ConsoleBufferName&) -> std::wstring { return L"- console"; },
          [](const ServerBufferName& input) -> std::wstring {
            return L"@ " + to_wstring(input);
          },
          [](const CommandBufferName& input) -> std::wstring {
            return L"$ " + to_wstring(input);
          },
          [](const AnonymousBufferName& input) -> std::wstring {
            return L"anonymous buffer " + to_wstring(input);
          },
          [](const std::wstring& str) { return L"[" + str + L"]"; },
      },
      p);
}
std::ostream& operator<<(std::ostream& os, const BufferName& p) {
  os << to_wstring(p);
  return os;
}

}  // namespace afc::editor
