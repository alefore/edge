#include "src/language/text/line_sequence_functional.h"

#include "src/language/container.h"
#include "src/language/text/mutable_line_sequence.h"

namespace afc::language::text {
using ::operator<<;

LineSequence FilterLines(
    LineSequence input,
    const std::function<FilterPredicateResult(const language::text::Line&)>&
        predicate) {
  MutableLineSequence builder;
  // TODO(2026-05-08, P1): Don't swallow the original origin. Preserve it.
  builder.append_back(input | std::views::filter([&](const Line& line) {
                        return predicate(line) == FilterPredicateResult::Keep;
                      }) |
                      staging::AddOrigin(staging::Clean));
  if (builder.size() > LineNumberDelta(1))
    builder.EraseLines(LineNumber(), LineNumber(1));
  LOG(INFO) << "Output: [" << builder.snapshot().ToLazyString() << "]";
  return builder.snapshot();
}
}  // namespace afc::language::text
