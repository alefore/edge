#ifndef __AFC_LANGUAGE_TEXT_LINE_COLUMN_H__
#define __AFC_LANGUAGE_TEXT_LINE_COLUMN_H__

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/tests/fuzz.h"

namespace afc::language::text {
GHOST_TYPE_NUMBER_WITH_DELTA(LineNumber, size_t, LineNumberDelta, int);
}  // namespace afc::language::text
GHOST_TYPE_TOP_LEVEL(afc::language::text::LineNumber)
GHOST_TYPE_TOP_LEVEL(afc::language::text::LineNumberDelta)
namespace afc::language::text {

// First adds the line, then adds the column.
struct LineColumnDelta {
  LineColumnDelta(LineNumberDelta input_line,
                  afc::language::lazy_string::ColumnNumberDelta input_column);
  LineColumnDelta() = default;

  LineNumberDelta line;
  afc::language::lazy_string::ColumnNumberDelta column;
};

std::ostream& operator<<(std::ostream& os, const LineColumnDelta& lc);
bool operator==(const LineColumnDelta& a, const LineColumnDelta& b);
bool operator!=(const LineColumnDelta& a, const LineColumnDelta& b);
bool operator<(const LineColumnDelta& a, const LineColumnDelta& b);
}  // namespace afc::language::text
namespace afc::tests::fuzz {
template <>
struct Reader<afc::language::text::LineNumber> {
  static std::optional<afc::language::text::LineNumber> Read(
      fuzz::Stream& input_stream);
};

template <>
struct Reader<afc::language::lazy_string::ColumnNumber> {
  static std::optional<afc::language::lazy_string::ColumnNumber> Read(
      fuzz::Stream& input_stream);
};
}  // namespace afc::tests::fuzz
namespace afc::language::text {
// A position in a text buffer.
struct LineColumn {
  LineColumn() = default;
  explicit LineColumn(LineNumber l)
      : LineColumn(l, afc::language::lazy_string::ColumnNumber(0)) {}
  LineColumn(LineNumber l, afc::language::lazy_string::ColumnNumber c)
      : line(l), column(c) {}

  static LineColumn Max() {
    return LineColumn(
        std::numeric_limits<LineNumber>::max(),
        std::numeric_limits<afc::language::lazy_string::ColumnNumber>::max());
  }

  bool operator!=(const LineColumn& other) const;

  std::wstring ToString() const;
  std::wstring Serialize() const;

  bool operator==(const LineColumn& rhs) const {
    return line == rhs.line && column == rhs.column;
  }

  bool operator<(const LineColumn& rhs) const {
    return line < rhs.line || (line == rhs.line && column < rhs.column);
  }

  bool operator<=(const LineColumn& rhs) const {
    return *this < rhs || *this == rhs;
  }

  bool operator>(const LineColumn& rhs) const { return rhs < *this; }

  bool operator>=(const LineColumn& rhs) const { return rhs <= *this; }

  LineColumn operator+(const LineNumberDelta& value) const;
  LineColumn operator-(const LineNumberDelta& value) const;
  LineColumn& operator+=(const LineNumberDelta& value);
  LineColumn& operator-=(const LineNumberDelta& value);

  LineColumn operator+(
      const afc::language::lazy_string::ColumnNumberDelta& value) const;
  LineColumn operator-(
      const afc::language::lazy_string::ColumnNumberDelta& value) const;

  LineColumn operator+(const LineColumnDelta& value) const;

  std::wstring ToCppString() const;

  LineNumber line;
  afc::language::lazy_string::ColumnNumber column;

  friend std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
};

std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
}  // namespace afc::language::text
namespace afc::tests::fuzz {
template <>
struct Reader<afc::language::text::LineColumn> {
  static std::optional<afc::language::text::LineColumn> Read(
      fuzz::Stream& input_stream);
};
}  // namespace afc::tests::fuzz

namespace afc::language::text {
// A range that contains every position i such that begin <= i < end.
struct Range {
  Range() = default;
  Range(LineColumn input_begin, LineColumn input_end);

  static Range InLine(LineColumn start,
                      afc::language::lazy_string::ColumnNumberDelta size);

  static Range InLine(LineNumber line,
                      afc::language::lazy_string::ColumnNumber column,
                      afc::language::lazy_string::ColumnNumberDelta size);

  template <typename Callback>
  void ForEachLine(Callback callback) {
    for (LineNumber line = begin_.line; line <= end_.line; ++line)
      callback(line);
  }

  bool IsEmpty() const { return begin_ >= end_; }

  bool Contains(const Range& subset) const;
  bool Contains(const LineColumn& position) const;
  bool Disjoint(const Range& other) const;

  // Returns the union, unless there's a gap between the ranges.
  language::ValueOrError<Range> Union(const Range& other) const;

  Range Intersection(const Range& other) const;
  bool operator==(const Range& rhs) const;

  LineNumberDelta lines() const;

  bool IsSingleLine() const;

  LineColumn begin() const;
  void set_begin(LineColumn value);
  void set_begin_line(LineNumber value);
  void set_begin_column(afc::language::lazy_string::ColumnNumber value);

  LineColumn end() const;
  void set_end(LineColumn value);
  void set_end_line(LineNumber value);
  void set_end_column(afc::language::lazy_string::ColumnNumber value);

 private:
  LineColumn begin_;
  LineColumn end_;
};

std::ostream& operator<<(std::ostream& os, const Range& range);

bool operator<(const Range& a, const Range& b);

}  // namespace afc::language::text
namespace std {
template <>
struct hash<afc::language::text::LineColumn> {
  std::size_t operator()(
      const afc::language::text::LineColumn& line_column) const {
    return afc::language::compute_hash(line_column.line, line_column.column);
  }
};
template <>
struct hash<afc::language::text::Range> {
  std::size_t operator()(const afc::language::text::Range& range) const {
    return afc::language::compute_hash(range.begin(), range.end());
  }
};
}  // namespace std

#endif  // __AFC_LANGUAGE_TEXT_LINE_COLUMN_H__
