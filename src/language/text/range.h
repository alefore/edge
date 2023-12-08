#ifndef __AFC_LANGUAGE_TEXT_RANGE_H__
#define __AFC_LANGUAGE_TEXT_RANGE_H__

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
struct hash<afc::language::text::Range> {
  std::size_t operator()(const afc::language::text::Range& range) const {
    return afc::language::compute_hash(range.begin(), range.end());
  }
};
}  // namespace std
#endif  // __AFC_LANGUAGE_TEXT_RANGE_H__
