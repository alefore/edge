#include "src/language/text/line_sequence_functional.h"

#include "src/language/text/mutable_line_sequence.h"

namespace afc::language::text {
using ::operator<<;

LineSequence FilterLines(
    LineSequence input,
    const std::function<FilterPredicateResult(const language::text::Line&)>&
        predicate) {
  MutableLineSequence builder;
  input.ForEach([&](const NonNull<std::shared_ptr<const Line>>& line) {
    switch (predicate(line.value())) {
      case FilterPredicateResult::kKeep:
        builder.push_back(line);
        break;
      case FilterPredicateResult::kErase:
        break;
    }
  });
  if (builder.size() > LineNumberDelta(1))
    builder.EraseLines(LineNumber(), LineNumber(1));
  LOG(INFO) << "Output: [" << builder.snapshot().ToString() << "]";
  return builder.snapshot();
}
}  // namespace afc::language::text
