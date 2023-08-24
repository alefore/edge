#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/error/value_or_error.h"
#include "src/tests/fuzz.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
GHOST_TYPE_NUMBER_WITH_DELTA(LineNumber, size_t, LineNumberDelta, int);
}  // namespace afc::editor
GHOST_TYPE_TOP_LEVEL(afc::editor::LineNumber)
GHOST_TYPE_TOP_LEVEL(afc::editor::LineNumberDelta)
namespace afc::editor {

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

namespace fuzz {
template <>
struct Reader<LineNumber> {
  static std::optional<LineNumber> Read(fuzz::Stream& input_stream);
};

template <>
struct Reader<afc::language::lazy_string::ColumnNumber> {
  static std::optional<afc::language::lazy_string::ColumnNumber> Read(
      fuzz::Stream& input_stream);
};
}  // namespace fuzz

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

namespace fuzz {
template <>
struct Reader<LineColumn> {
  static std::optional<LineColumn> Read(fuzz::Stream& input_stream);
};
}  // namespace fuzz

// A range that contains every position i such that begin <= i < end.
struct Range {
  Range() = default;
  Range(LineColumn input_begin, LineColumn input_end)
      : begin(input_begin), end(input_end) {}

  static Range InLine(LineNumber line,
                      afc::language::lazy_string::ColumnNumber column,
                      afc::language::lazy_string::ColumnNumberDelta size);

  template <typename Callback>
  void ForEachLine(Callback callback) {
    for (LineNumber line = begin.line; line < end.line; ++line) {
      callback(line);
    }
  }

  bool IsEmpty() const { return begin >= end; }

  bool Contains(const Range& subset) const {
    return begin <= subset.begin && subset.end <= end;
  }

  bool Contains(const LineColumn& position) const {
    return begin <= position && position < end;
  }

  bool Disjoint(const Range& other) const {
    return end <= other.begin || other.end <= begin;
  }

  // Returns the union, unless there's a gap between the ranges.
  language::ValueOrError<Range> Union(const Range& other) const;

  Range Intersection(const Range& other) const {
    if (Disjoint(other)) {
      return Range();
    }
    return Range(std::max(begin, other.begin), std::min(end, other.end));
  }

  bool operator==(const Range& rhs) const {
    return begin == rhs.begin && end == rhs.end;
  }

  LineNumberDelta lines() const {
    return end.line - begin.line + LineNumberDelta(1);
  }

  LineColumn begin;
  LineColumn end;
};

std::ostream& operator<<(std::ostream& os, const Range& range);

bool operator<(const Range& a, const Range& b);

}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::LineColumn> {
  std::size_t operator()(const afc::editor::LineColumn& line_column) const {
    return afc::language::compute_hash(line_column.line, line_column.column);
  }
};
template <>
struct hash<afc::editor::Range> {
  std::size_t operator()(const afc::editor::Range& range) const {
    return afc::language::compute_hash(range.begin, range.end);
  }
};
}  // namespace std

#endif
