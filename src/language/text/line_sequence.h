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

  static LineSequence ForTests(std::vector<std::wstring> inputs);
  static LineSequence WithLine(Line line);

  // Returns a new LineSequence that contains a subset of the current one.
  LineSequence ViewRange(language::text::Range range) const;

  lazy_string::LazyString ToLazyString() const;
  std::wstring ToString() const;

  LineNumberDelta size() const;
  bool empty() const;
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

 private:
  friend class MutableLineSequence;
  friend class SortedLineSequence;
  friend class LineSequenceIterator;
  LineSequence(NonNull<Lines::Ptr> lines) : lines_(lines) {}
};

class LineSequenceIterator {
 private:
  struct Data {
    LineSequence container;
    LineNumber position;
    bool operator==(const Data& other) const {
      return container.lines_ == other.container.lines_ &&
             position == other.position;
    }
  };
  std::optional<Data> data_;

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
      : data_(Data{.container = std::move(container), .position = position}) {}

  Line operator*() const {
    CHECK(data_.has_value());
    return data_->container.at(data_->position);
  }

  bool operator!=(const LineSequenceIterator& other) const {
    return !(*this == other);
  }

  bool operator==(const LineSequenceIterator& other) const {
    if (IsAtEnd() || other.IsAtEnd()) return IsAtEnd() && other.IsAtEnd();
    return data_.value() == other.data_.value();
  }

  LineSequenceIterator& operator++() {  // Prefix increment.
    ++data_->position;
    // TODO: If we get to the end, we must reset data_->container.
    return *this;
  }

  LineSequenceIterator operator++(int) {  // Postfix increment.
    LineSequenceIterator tmp = *this;
    ++*this;
    return tmp;
  }

  int operator-(const LineSequenceIterator& other) const {
    CHECK(data_->container.lines_ == other.data_->container.lines_);
    return (data_->position - other.data_->position).read();
  }

  LineSequenceIterator operator+(int n) const {
    return LineSequenceIterator(data_->container,
                                data_->position + LineNumberDelta(n));
  }

  LineSequenceIterator operator+(int n) {
    return LineSequenceIterator(data_->container,
                                data_->position + LineNumberDelta(n));
  }

 private:
  bool IsAtEnd() const {
    return data_ == std::nullopt ||
           data_->position.ToDelta() >= data_->container.size();
  }
};

}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
