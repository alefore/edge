#include "src/buffer_name.h"

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"

using afc::language::overload;
using afc::language::to_wstring;
using afc::language::lazy_string::LazyString;

namespace afc::editor {

using ::operator<<;

LazyString ToLazyString(const BufferName& p) {
  return std::visit(
      overload{
          [](const BufferFileId& i) { return LazyString{to_wstring(i)}; },
          [](const BufferListId&) { return LazyString{L"- buffers"}; },
          [](const PasteBuffer&) { return LazyString{L"- paste buffer"}; },
          [](const FuturePasteBuffer&) {
            return LazyString{L"- future paste buffer"};
          },
          [](const TextInsertion&) { return LazyString{L"- text inserted"}; },
          [](const InitialCommands&) {
            return LazyString{L"- initial commands"};
          },
          [](const ConsoleBufferName&) { return LazyString{L"- console"}; },
          [](const PredictionsBufferName&) {
            return LazyString{L"- predictions"};
          },
          [](const HistoryBufferName& input) {
            return LazyString{L"- history: "} + LazyString{to_wstring(input)};
          },
          [](const ServerBufferName& input) {
            return LazyString{L"@ "} + LazyString{to_wstring(input)};
          },
          [](const CommandBufferName& input) {
            return LazyString{L"$ "} + LazyString{to_wstring(input)};
          },
          [](const AnonymousBufferName& input) {
            return LazyString{L"anonymous buffer "} +
                   LazyString{to_wstring(input)};
          },
          [](const std::wstring& str) {
            return LazyString{L"["} + LazyString{str} + LazyString{L"]"};
          },
      },
      p);
}
std::ostream& operator<<(std::ostream& os, const BufferName& p) {
  os << ToLazyString(p).ToString();
  return os;
}

}  // namespace afc::editor
