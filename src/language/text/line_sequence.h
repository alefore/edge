#ifndef __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
#define __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__

#include <memory>
#include <vector>

#include "src/language/const_tree.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/range.h"
#include "src/tests/fuzz_testable.h"

namespace afc::language::text {
class MutableLineSequence;
class SortedLineSequence;
class LineSequenceIterator;

// TODO: Add more methods here.

// A non-empty sequence of lines.
//
// Non-emptyness is ensured statically through the use of NonNull<> types.
//
// This class is thread-compatible.
class LineSequence {
 private:
  using Lines = language::ConstTree<language::VectorBlock<Line, 256>, 256>;

  NonNull<Lines::Ptr> lines_ = Lines::PushBack(nullptr, Line());

 public:
  LineSequence() = default;
  LineSequence(const LineSequence&) = default;
  LineSequence(LineSequence&&) = default;
  LineSequence& operator=(const LineSequence&) = default;

  LineSequence(LineSequenceIterator a, LineSequenceIterator b);
  static LineSequence ForTests(std::vector<std::wstring> inputs);
  static LineSequence WithLine(Line line);
  static LineSequence BreakLines(lazy_string::LazyString);

  // Returns a new LineSequence that contains a subset of the current one.
  LineSequence ViewRange(language::text::Range range) const;

  lazy_string::LazyString ToLazyString() const;
  std::wstring ToString() const;

  LineNumberDelta size() const;
  // The last valid line (which can be fed to `at`).
  LineNumber EndLine() const;
  Range range() const;

  size_t CountCharacters() const;

  const Line& at(LineNumber line_number) const;
  const Line& back() const;
  const Line& front() const;

  // Iterates: runs the callback on every line in the buffer intersecting the
  // range, passing as the first argument the line count (starts counting at 0).
  // Stops the iteration if the callback returns false. Returns true iff the
  // callback always returned true.
  bool ForEachLine(
      LineNumber start, LineNumberDelta length,
      const std::function<bool(LineNumber, const Line&)>& callback) const;

  // Convenience wrappers of the above.
  bool EveryLine(
      const std::function<bool(LineNumber, const Line&)>& callback) const;

  void ForEach(const std::function<void(const Line&)>& callback) const;
  void ForEach(const std::function<void(std::wstring)>& callback) const;

  LineSequence Map(const std::function<Line(const Line&)>&) const;

  wint_t character_at(const LineColumn& position) const;

  LineColumn AdjustLineColumn(LineColumn position) const;

  language::text::LineColumn PositionBefore(
      language::text::LineColumn position) const;
  language::text::LineColumn PositionAfter(
      language::text::LineColumn position) const;

  LineSequenceIterator begin() const;
  LineSequenceIterator end() const;

  bool operator==(const LineSequence& other) const;

 private:
  friend class MutableLineSequence;
  friend class SortedLineSequence;
  friend class LineSequenceIterator;
  LineSequence(NonNull<Lines::Ptr> lines) : lines_(lines) {}
};

class LineSequenceIterator {
 private:
  LineSequence container_;
  LineNumber position_;

 public:
  LineSequenceIterator() {}
  LineSequenceIterator(const LineSequenceIterator&) = default;
  LineSequenceIterator(LineSequenceIterator&&) = default;
  LineSequenceIterator& operator=(const LineSequenceIterator&) = default;
  LineSequenceIterator& operator=(LineSequenceIterator&&) = default;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = Line;
  using reference = value_type&;

  LineSequenceIterator(LineSequence container, LineNumber position)
      : container_(std::move(container)), position_(position) {}

  Line operator*() const { return container_.at(position_); }

  bool operator!=(const LineSequenceIterator& other) const {
    return !(*this == other);
  }

  bool operator==(const LineSequenceIterator& other) const {
    if (IsAtEnd() || other.IsAtEnd()) return IsAtEnd() && other.IsAtEnd();
    return container_.lines_ == other.container_.lines_ &&
           position_ == other.position_;
  }

  LineSequenceIterator& operator++() {  // Prefix increment.
    ++position_;
    return *this;
  }

  LineSequenceIterator operator++(int) {  // Postfix increment.
    LineSequenceIterator tmp = *this;
    ++*this;
    return tmp;
  }

  LineSequenceIterator& operator--();

  int operator-(const LineSequenceIterator& other) const {
    CHECK(container_.lines_ == other.container_.lines_);
    return (position_ - other.position_).read();
  }

  LineSequenceIterator operator+(int n) const {
    return LineSequenceIterator(container_, position_ + LineNumberDelta(n));
  }

  LineSequenceIterator operator+(int n) {
    return LineSequenceIterator(container_, position_ + LineNumberDelta(n));
  }

 private:
  bool IsAtEnd() const { return position_.ToDelta() >= container_.size(); }
};

}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
