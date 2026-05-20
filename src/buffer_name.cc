#include "src/buffer_name.h"

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"
#include "src/vm/escape.h"

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::overload;
using afc::language::to_wstring;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineSequence;
using afc::vm::EscapedString;

namespace afc::editor {

using ::operator<<;

FilterBufferName::FilterBufferName(NonEmptySingleLine input_source_buffer,
                                   std::vector<SingleLine> input_filters)
    : source_buffer(std::move(input_source_buffer)),
      filters(std::move(input_filters)) {}

FilterBufferName FilterBufferNameFactory::New(BufferName source_buffer_variant,
                                              SingleLine filter) {
  if (FilterBufferName* source_buffer =
          std::get_if<FilterBufferName>(&source_buffer_variant);
      source_buffer != nullptr) {
    source_buffer->filters.push_back(filter);
    return *source_buffer;
  }
  return FilterBufferName(ToSingleLine(source_buffer_variant),
                          std::vector{filter});
}

namespace {
NonEmptySingleLine VisualizePath(const Path& path) {
  return Visit(
      NonEmptySingleLine::New(
          EscapedString::FromString(path.read()).EscapedRepresentation()),
      [](NonEmptySingleLine output) { return output; },
      [](Error) { return NON_EMPTY_SINGLE_LINE_CONSTANT(L"-"); });
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
          [](const PreviewBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"(") +
                   EscapedString::FromString(input.read())
                       .EscapedRepresentation() +
                   NON_EMPTY_SINGLE_LINE_CONSTANT(L")");
          },
          [](const AnonymousBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"anonymous buffer ") +
                   NonEmptySingleLine{input.read()}.read();
          },
          [](const FilterBufferName& input) -> NonEmptySingleLine {
            return NON_EMPTY_SINGLE_LINE_CONSTANT(L"- filter") +
                   input.source_buffer + NON_EMPTY_SINGLE_LINE_CONSTANT(L": ") +
                   Concatenate(input.filters |
                               Intersperse(SINGLE_LINE_CONSTANT(L", ")));
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
