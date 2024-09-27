#include "src/buffer_name.h"

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"
#include "src/vm/escape.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::overload;
using afc::language::to_wstring;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineSequence;
using afc::vm::EscapedString;

namespace afc::editor {

using ::operator<<;

namespace {
NonEmptySingleLine VisualizePath(const Path& path) {
  return std::visit(
      overload{[](NonEmptySingleLine output) { return output; },
               [](Error) { return NON_EMPTY_SINGLE_LINE_CONSTANT(L"-"); }},
      NonEmptySingleLine::New(
          EscapedString::FromString(path.read()).EscapedRepresentation()));
}
}  // namespace

NonEmptySingleLine ToSingleLine(const BufferName& p) {
  return std::visit(
      overload{
          [](const BufferFileId& i) -> NonEmptySingleLine {
            return VisualizePath(i.read());
          },
          [](const BufferListId&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- buffers");
          },
          [](const FragmentsBuffer&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- fragments");
          },
          [](const PasteBuffer&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- paste buffer");
          },
          [](const FuturePasteBuffer&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- future paste buffer");
          },
          [](const TextInsertion&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- text inserted");
          },
          [](const InitialCommands&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- initial commands");
          },
          [](const ConsoleBufferName&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- console");
          },
          [](const PredictionsBufferName&) {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- predictions");
          },
          [](const HistoryBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- history: ") +
                   input.read().read();
          },
          [](const ServerBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"@ ") +
                   VisualizePath(input.read());
          },
          [](const CommandBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"$ ") +
                   EscapedString::FromString(input.read())
                       .EscapedRepresentation();
          },
          [](const AnonymousBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"anonymous buffer ") +
                   NonEmptySingleLine{input.read()}.read();
          },
          [](const LazyString& str) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"[") +
                   LineSequence::BreakLines(str).FoldLines() +
                   NON_EMPTY_SINGLE_LINE_CONSTANT(L"]");
          },
      },
      p);
}
std::ostream& operator<<(std::ostream& os, const BufferName& p) {
  os << ToSingleLine(p);
  return os;
}

}  // namespace afc::editor
