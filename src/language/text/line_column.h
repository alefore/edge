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
namespace std {
template <>
struct hash<afc::language::text::LineColumn> {
  std::size_t operator()(
      const afc::language::text::LineColumn& line_column) const {
    return afc::language::compute_hash(line_column.line, line_column.column);
  }
};
}  // namespace std

#endif  // __AFC_LANGUAGE_TEXT_LINE_COLUMN_H__
