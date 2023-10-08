#include "src/language/text/sorted_line_sequence.h"

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/text/mutable_line_sequence.h"

using afc::language::lazy_string::LowerCase;

namespace afc::language::text {
SortedLineSequence::SortedLineSequence(LineSequence input)
    : SortedLineSequence(input,
                         [](const NonNull<std::shared_ptr<const Line>>& a,
                            const NonNull<std::shared_ptr<const Line>>& b) {
                           return LowerCase(a->contents()).value() <
                                  LowerCase(b->contents()).value();
                         }) {}

SortedLineSequence::SortedLineSequence(LineSequence input,
                                       SortedLineSequence::Compare compare)
    : lines_([&] {
        TRACK_OPERATION(SortedLineSequence_SortedLineSequence);
        std::vector<
            language::NonNull<std::shared_ptr<const language::text::Line>>>
            lines;
        input.ForEach(
            [&lines](const language::NonNull<
                     std::shared_ptr<const language::text::Line>>& line) {
              lines.push_back(line);
            });
        std::sort(lines.begin(), lines.end(), compare);
        MutableLineSequence builder;
        for (auto& line : lines) builder.push_back(std::move(line));
        if (builder.size() > LineNumberDelta(1))
          builder.EraseLines(LineNumber(), LineNumber(1));
        return builder.snapshot();
      }()),
      compare_(std::move(compare)) {}
}  // namespace afc::language::text
