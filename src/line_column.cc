#include "src/line_column.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/lazy_string.h"
#include "src/line_column_vm.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

NonNull<std::shared_ptr<LazyString>> PaddingString(
    const ColumnNumberDelta& length, wchar_t fill) {
  if (length < ColumnNumberDelta(0)) {
    return EmptyString();
  }
  return NewLazyString(length, fill);
}

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

namespace fuzz {
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
}  // namespace fuzz

namespace fuzz {
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
}  // namespace fuzz

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

/* static */ Range Range::InLine(LineNumber line, ColumnNumber column,
                                 ColumnNumberDelta size) {
  return Range(LineColumn(line, column), LineColumn(line, column + size));
}

language::ValueOrError<Range> Range::Union(const Range& other) const {
  if (end < other.begin || begin > other.end)
    return language::Error(L"Gap found between the ranges.");
  return Range(std::min(begin, other.begin), std::max(end, other.end));
}

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin << ", " << range.end << ")";
  return os;
}

bool operator<(const Range& a, const Range& b) {
  return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
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

std::wstring LineColumn::ToCppString() const {
  return L"LineColumn(" + to_wstring(line) + L", " + to_wstring(column) + L")";
}

namespace fuzz {
/* static */ std::optional<LineColumn> Reader<LineColumn>::Read(
    fuzz::Stream& input_stream) {
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
}  // namespace fuzz
}  // namespace afc::editor
