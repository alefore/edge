#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "src/fuzz.h"
#include "src/language/hash.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"

namespace afc::language::gc {
class Pool;
}
namespace afc {
namespace editor {

class LazyString;

GHOST_TYPE_NUMBER_WITH_DELTA(LineNumber, size_t, LineNumberDelta, int);
GHOST_TYPE_NUMBER_WITH_DELTA(ColumnNumber, size_t, ColumnNumberDelta, int);

// Generates a string of the length specified by `this` filled up with the
// character given.
//
// If length is negative (or zero), returns an empty string.
//
// TODO(easy, 2022-06-05): Move this to a LazyString-related module?
language::NonNull<std::shared_ptr<LazyString>> PaddingString(
    const ColumnNumberDelta& length, wchar_t fill);

// First adds the line, then adds the column.
struct LineColumnDelta {
  LineColumnDelta(LineNumberDelta input_line, ColumnNumberDelta input_column);
  LineColumnDelta() = default;

  LineNumberDelta line;
  ColumnNumberDelta column;
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
}  // namespace fuzz

namespace fuzz {
template <>
struct Reader<ColumnNumber> {
  static std::optional<ColumnNumber> Read(fuzz::Stream& input_stream);
};
}  // namespace fuzz

}  // namespace editor
}  // namespace afc
namespace std {
template <>
class numeric_limits<afc::editor::LineNumber> {
 public:
  static afc::editor::LineNumber max() {
    return afc::editor::LineNumber(std::numeric_limits<size_t>::max());
  };
};
template <>
class numeric_limits<afc::editor::ColumnNumber> {
 public:
  static afc::editor::ColumnNumber max() {
    return afc::editor::ColumnNumber(std::numeric_limits<size_t>::max());
  };
};
}  // namespace std
namespace afc::editor {
// A position in a text buffer.
struct LineColumn {
  LineColumn() = default;
  explicit LineColumn(LineNumber l) : LineColumn(l, ColumnNumber(0)) {}
  LineColumn(LineNumber l, ColumnNumber c) : line(l), column(c) {}

  static LineColumn Max() {
    return LineColumn(std::numeric_limits<LineNumber>::max(),
                      std::numeric_limits<ColumnNumber>::max());
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

  LineColumn operator+(const ColumnNumberDelta& value) const;
  LineColumn operator-(const ColumnNumberDelta& value) const;

  LineColumn operator+(const LineColumnDelta& value) const;

  std::wstring ToCppString() const;

  LineNumber line;
  ColumnNumber column;

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

  static Range InLine(LineNumber line, ColumnNumber column,
                      ColumnNumberDelta size);

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
struct hash<afc::editor::ColumnNumberDelta> {
  std::size_t operator()(const afc::editor::ColumnNumberDelta& delta) const {
    return std::hash<size_t>()(delta.read());
  }
};
template <>
struct hash<afc::editor::LineNumberDelta> {
  std::size_t operator()(const afc::editor::LineNumberDelta& delta) const {
    return std::hash<size_t>()(delta.read());
  }
};
template <>
struct hash<afc::editor::ColumnNumber> {
  std::size_t operator()(const afc::editor::ColumnNumber& column) const {
    return std::hash<size_t>()(column.read());
  }
};
template <>
struct hash<afc::editor::LineNumber> {
  std::size_t operator()(const afc::editor::LineNumber& line) const {
    return std::hash<size_t>()(line.read());
  }
};
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
