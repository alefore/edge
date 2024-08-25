#include "src/language/text/sorted_line_sequence.h"

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/text/mutable_line_sequence.h"

using afc::language::lazy_string::LowerCase;

namespace afc::language::text {
SortedLineSequence::SortedLineSequence(LineSequence input)
    : SortedLineSequence(input, [](const Line& a, const Line& b) {
        return LowerCase(a.contents()) < LowerCase(b.contents());
      }) {}

SortedLineSequence::SortedLineSequence(LineSequence input,
                                       SortedLineSequence::Compare compare)
    : SortedLineSequence(
          TrustedConstructorTag(),
          [&] {
            if (input.empty()) return input;
            TRACK_OPERATION(SortedLineSequence_sort);
            std::vector<Line> lines = container::MaterializeVector(input);
            std::sort(lines.begin(), lines.end(), compare);
            MutableLineSequence builder;
            builder.append_back(std::move(lines));
            if (builder.size() > LineNumberDelta(1))
              builder.EraseLines(LineNumber(), LineNumber(1));
            return builder.snapshot();
          }(),
          compare) {}

SortedLineSequence::SortedLineSequence(TrustedConstructorTag,
                                       LineSequence lines, Compare compare)
    : lines_(std::move(lines)), compare_(std::move(compare)) {}

const LineSequence& SortedLineSequence::lines() const { return lines_; }

LineNumber SortedLineSequence::upper_bound(const Line& key) const {
  return LineNumber(LineSequence::Lines::UpperBound(lines_.lines_.get_shared(),
                                                    key, compare_));
}

SortedLineSequence SortedLineSequence::FilterLines(
    const std::function<FilterPredicateResult(const Line&)>& predicate) const {
  return SortedLineSequence(TrustedConstructorTag(),
                            text::FilterLines(lines_, predicate), compare_);
}

SortedLineSequenceUniqueLines::SortedLineSequenceUniqueLines(
    SortedLineSequence sorted_lines)
    : SortedLineSequenceUniqueLines(
          TrustedConstructorTag{}, std::invoke([&] {
            MutableLineSequence builder;
            sorted_lines.lines().ForEach([&builder](const Line& line) {
              if (builder.size().IsZero() || !(builder.back() == line))
                builder.push_back(line);
            });
            if (builder.size() > LineNumberDelta(1))
              builder.EraseLines(LineNumber(), LineNumber(1));
            return SortedLineSequence(
                SortedLineSequence::TrustedConstructorTag{}, builder.snapshot(),
                sorted_lines.compare_);
          })) {}

SortedLineSequenceUniqueLines::SortedLineSequenceUniqueLines(
    TrustedConstructorTag, SortedLineSequence sorted_lines)
    : GhostType<SortedLineSequenceUniqueLines, SortedLineSequence>(
          std::move(sorted_lines)) {}

SortedLineSequenceUniqueLines::SortedLineSequenceUniqueLines(
    SortedLineSequenceUniqueLines a, SortedLineSequenceUniqueLines b)
    : SortedLineSequenceUniqueLines(TrustedConstructorTag{}, [&] {
        LineNumber a_line;
        LineNumber b_line;
        const LineSequence& a_lines = a.read().lines();
        const LineSequence& b_lines = b.read().lines();
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
            const Line& a_str = a_lines.at(a_line);
            const Line& b_str = b_lines.at(b_line);
            if (a.read().compare_(a_str, b_str)) {
              advance(a_lines, a_line);
            } else if (a.read().compare_(b_str, a_str)) {
              advance(b_lines, b_line);
            } else {
              advance(a_lines, a_line);
              ++b_line;
            }
          }
        }
        return SortedLineSequence(SortedLineSequence::TrustedConstructorTag(),
                                  builder.snapshot(), a.read().compare_);
      }()) {}
}  // namespace afc::language::text
