#include "src/language/text/line_column.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/wstring.h"
#include "src/vm/environment.h"

namespace gc = afc::language::gc;

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::language::text {
LineColumnDelta::LineColumnDelta(LineNumberDelta input_line,
                                 ColumnNumberDelta input_column)
    : line(input_line), column(input_column) {}

std::ostream& operator<<(std::ostream& os, const LineColumnDelta& lc) {
  os << "[" << lc.line << " " << lc.column << "]";
  return os;
}

bool operator==(const LineColumnDelta& a, const LineColumnDelta& b) {
  return a.line == b.line && a.column == b.column;
}

bool operator!=(const LineColumnDelta& a, const LineColumnDelta& b) {
  return !(a == b);
}

bool operator<(const LineColumnDelta& a, const LineColumnDelta& b) {
  return a.line < b.line || (a.line == b.line && a.column < b.column);
}
}  // namespace afc::language::text
namespace afc::tests::fuzz {
using language::lazy_string::ColumnNumber;
using language::text::LineNumber;

std::optional<LineNumber> Reader<LineNumber>::Read(Stream& input_stream) {
  auto value = Reader<size_t>::Read(input_stream);
  if (!value.has_value()) {
    VLOG(8) << "Fuzz: LineNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = LineNumber(value.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
};

std::optional<ColumnNumber> Reader<ColumnNumber>::Read(Stream& input_stream) {
  auto value = Reader<size_t>::Read(input_stream);
  if (!value.has_value()) {
    VLOG(8) << "Fuzz: ColumnNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = ColumnNumber(value.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
};
}  // namespace afc::tests::fuzz
namespace afc::language::text {
LineColumn LineColumn::NextLine() const {
  return LineColumn{line + LineNumberDelta{1}};
}

std::ostream& operator<<(std::ostream& os, const LineColumn& lc) {
  os << "["
     << (lc.line == std::numeric_limits<LineNumber>::max()
             ? "inf"
             : std::to_string(lc.line.read()))
     << ":"
     << (lc.column == std::numeric_limits<ColumnNumber>::max()
             ? "inf"
             : std::to_string(lc.column.read()))
     << "]";
  return os;
}

bool LineColumn::operator!=(const LineColumn& other) const {
  return line != other.line || column != other.column;
}

std::wstring LineColumn::ToString() const {
  return to_wstring(line) + L" " + to_wstring(column);
}

std::wstring LineColumn::Serialize() const {
  return L"LineColumn(" + to_wstring(line) + L", " + to_wstring(column) + L")";
}

LineColumn LineColumn::operator+(const LineNumberDelta& value) const {
  auto output = *this;
  output += value;
  return output;
}

LineColumn LineColumn::operator-(const LineNumberDelta& value) const {
  return *this + -value;
}

LineColumn& LineColumn::operator+=(const LineNumberDelta& value) {
  line += value;
  return *this;
}

LineColumn& LineColumn::operator-=(const LineNumberDelta& value) {
  return operator+=(-value);
}

LineColumn LineColumn::operator+(const ColumnNumberDelta& value) const {
  return LineColumn(line, column + value);
}

LineColumn LineColumn::operator-(const ColumnNumberDelta& value) const {
  return *this + -value;
}

LineColumn LineColumn::operator+(const LineColumnDelta& value) const {
  return *this + value.line + value.column;
}

NonEmptySingleLine LineColumn::ToCppString() const {
  return SINGLE_LINE_CONSTANT(L"LineColumn(") +
         NonEmptySingleLine(line.read()) + SINGLE_LINE_CONSTANT(L", ") +
         NonEmptySingleLine(column.read()) + SINGLE_LINE_CONSTANT(L")");
}

}  // namespace afc::language::text
namespace afc::tests::fuzz {
using language::lazy_string::ColumnNumber;
using language::text::LineColumn;
using language::text::LineNumber;
/* static */ std::optional<LineColumn> Reader<LineColumn>::Read(
    Stream& input_stream) {
  auto line = Reader<LineNumber>::Read(input_stream);
  auto column = Reader<ColumnNumber>::Read(input_stream);
  if (!line.has_value() || !column.has_value()) {
    VLOG(8) << "Fuzz: LineNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = LineColumn(line.value(), column.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
}
}  // namespace afc::tests::fuzz
