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
    : SortedLineSequence(
          TrustedConstructorTag(),
          [&] {
            if (input.empty()) return input;
            TRACK_OPERATION(SortedLineSequence_sort);
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
          }(),
          compare) {}

SortedLineSequence::SortedLineSequence(TrustedConstructorTag,
                                       LineSequence lines, Compare compare)
    : lines_(std::move(lines)), compare_(std::move(compare)) {}

const LineSequence& SortedLineSequence::lines() const { return lines_; }

language::text::LineNumber SortedLineSequence::upper_bound(
    const language::NonNull<std::shared_ptr<const language::text::Line>>& key)
    const {
  return language::text::LineNumber(
      LineSequence::Lines::UpperBound(lines_.lines_, key, compare_));
}

SortedLineSequence SortedLineSequence::FilterLines(
    const std::function<FilterPredicateResult(const language::text::Line&)>&
        predicate) const {
  return SortedLineSequence(TrustedConstructorTag(),
                            text::FilterLines(lines_, predicate), compare_);
}

SortedLineSequenceUniqueLines::SortedLineSequenceUniqueLines(
    SortedLineSequence sorted_lines)
    : sorted_lines_(std::move(sorted_lines)) {}

SortedLineSequenceUniqueLines::SortedLineSequenceUniqueLines(
    SortedLineSequenceUniqueLines a, SortedLineSequenceUniqueLines b)
    : SortedLineSequenceUniqueLines([&] {
        LineNumber a_line;
        LineNumber b_line;
        const LineSequence& a_lines = a.sorted_lines().lines();
        const LineSequence& b_lines = b.sorted_lines().lines();
        MutableLineSequence builder;
        auto advance = [&](const LineSequence& input, LineNumber& line) {
          CHECK_LT(line.ToDelta(), input.size());
          builder.push_back(input.at(line));
          ++line;
        };
        while (a_line.ToDelta() < a_lines.size() ||
               b_line.ToDelta() < b_lines.size()) {
          if (a_line.ToDelta() == a_lines.size()) {
            advance(b_lines, b_line);
          } else if (b_line.ToDelta() == b_lines.size()) {
            advance(a_lines, a_line);
          } else {
            NonNull<std::shared_ptr<const Line>> a_str = a_lines.at(a_line);
            NonNull<std::shared_ptr<const Line>> b_str = b_lines.at(b_line);
            if (a.sorted_lines_.compare_(a_str, b_str)) {
              advance(a_lines, a_line);
            } else if (a.sorted_lines_.compare_(b_str, a_str)) {
              advance(b_lines, b_line);
            } else {
              advance(a_lines, a_line);
              ++b_line;
            }
          }
        }
        return SortedLineSequence(SortedLineSequence::TrustedConstructorTag(),
                                  builder.snapshot(), a.sorted_lines_.compare_);
      }()) {}
}  // namespace afc::language::text
