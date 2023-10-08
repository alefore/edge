#ifndef __AFC_LANGUAGE_TEXT_SORTED_LINE_SEQUENCE_H__
#define __AFC_LANGUAGE_TEXT_SORTED_LINE_SEQUENCE_H__

#include <memory>
#include <vector>

#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/line_sequence_functional.h"

namespace afc::language::text {
class SortedLineSequence {
 public:
  using Compare = std::function<bool(
      const language::NonNull<std::shared_ptr<const language::text::Line>>&,
      const language::NonNull<std::shared_ptr<const language::text::Line>>&)>;

  SortedLineSequence(const SortedLineSequence&) = default;
  SortedLineSequence(SortedLineSequence&&) = default;
  SortedLineSequence& operator=(const SortedLineSequence&) = default;

  explicit SortedLineSequence(LineSequence input);
  SortedLineSequence(LineSequence input, Compare compare);

  const LineSequence& lines() const { return lines_; }

  // Returns the first element such that key < element. Assumes that the
  // sequence is sorted.
  //
  // TODO(trivial, 2023-10-08): Move implementation to .cc file.
  language::text::LineNumber upper_bound(
      const language::NonNull<std::shared_ptr<const language::text::Line>>& key)
      const {
    return language::text::LineNumber(
        LineSequence::Lines::UpperBound(lines_.lines_, key, compare_));
  }

  SortedLineSequence FilterLines(
      const std::function<FilterPredicateResult(const language::text::Line&)>&
          predicate) const;

 private:
  struct TrustedConstructorTag {};
  SortedLineSequence(TrustedConstructorTag, LineSequence lines,
                     Compare compare);

  LineSequence lines_;
  Compare compare_;
};

// Similar to SortedLineSequence, but ensures that there are no duplicate lines.
class SortedLineSequenceUniqueLines {
 public:
  SortedLineSequenceUniqueLines(const SortedLineSequenceUniqueLines&) = default;
  SortedLineSequenceUniqueLines(SortedLineSequenceUniqueLines&&) = default;
  SortedLineSequenceUniqueLines& operator=(
      const SortedLineSequenceUniqueLines&) = default;

  explicit SortedLineSequenceUniqueLines(SortedLineSequence sorted_lines);

  const SortedLineSequence& sorted_lines() const { return sorted_lines_; }

 private:
  SortedLineSequence sorted_lines_;
};

}  // namespace afc::language::text
#endif
