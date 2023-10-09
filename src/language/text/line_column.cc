#include "src/language/text/line_column.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
#include "src/vm/environment.h"

namespace afc::language::text {
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::EmptyString;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

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

// TODO(easy?, 2023-09-18): Assert that input_begin <= input_end?
Range::Range(LineColumn input_begin, LineColumn input_end)
    : begin_(input_begin), end_(input_end) {}

/* static */ Range Range::InLine(
    LineColumn start, afc::language::lazy_string::ColumnNumberDelta size) {
  return Range(start, LineColumn(start.line, start.column + size));
}

/* static */ Range Range::InLine(LineNumber line, ColumnNumber column,
                                 ColumnNumberDelta size) {
  return InLine(LineColumn(line, column), size);
}

bool Range::Contains(const Range& subset) const {
  return begin() <= subset.begin() && subset.end() <= end();
}

bool Range::Contains(const LineColumn& position) const {
  return begin() <= position &&
         (position < end() ||
          // Handle the case where `end.column` is max: this should include
          // anything in the line. This matters when `position.column` is also
          // max.
          (position.line == end().line &&
           end().column == std::numeric_limits<ColumnNumber>::max()));
}

bool Range::Disjoint(const Range& other) const {
  return end() <= other.begin() || other.end() <= begin();
}

language::ValueOrError<Range> Range::Union(const Range& other) const {
  if (end() < other.begin() || begin() > other.end())
    return language::Error(L"Gap found between the ranges.");
  return Range(std::min(begin(), other.begin()), std::max(end(), other.end()));
}

Range Range::Intersection(const Range& other) const {
  if (Disjoint(other)) {
    return Range();
  }
  return Range(std::max(begin(), other.begin()), std::min(end(), other.end()));
}

bool Range::operator==(const Range& rhs) const {
  return begin() == rhs.begin() && end() == rhs.end();
}

LineNumberDelta Range::lines() const {
  return end().line - begin().line + LineNumberDelta(1);
}

bool Range::IsSingleLine() const { return begin_.line == end_.line; }

LineColumn Range::begin() const { return begin_; };
void Range::set_begin(LineColumn value) { begin_ = value; }
void Range::set_begin_line(LineNumber value) { begin_.line = value; }
void Range::set_begin_column(ColumnNumber value) { begin_.column = value; }

LineColumn Range::end() const { return end_; }
void Range::set_end(LineColumn value) { end_ = value; }
void Range::set_end_line(LineNumber value) { end_.line = value; }
void Range::set_end_column(ColumnNumber value) { end_.column = value; }

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin() << ", " << range.end() << ")";
  return os;
}

bool operator<(const Range& a, const Range& b) {
  return a.begin() < b.begin() || (a.begin() == b.begin() && a.end() < b.end());
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
