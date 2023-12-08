#include "src/language/text/range.h"

#include "src/language/text/line_column.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;

namespace afc::language::text {
Range::Range(LineColumn input_begin, LineColumn input_end)
    : begin_(input_begin), end_(input_end) {
  CHECK_LE(begin_, end_);
}

/* static */ Range Range::InLine(
    LineColumn start, afc::language::lazy_string::ColumnNumberDelta size) {
  CHECK_GE(size, ColumnNumberDelta(0));
  return Range(start, LineColumn(start.line, start.column + size));
}

/* static */ Range Range::InLine(LineNumber line, ColumnNumber column,
                                 ColumnNumberDelta size) {
  CHECK_GE(size, ColumnNumberDelta(0));
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
void Range::set_begin(LineColumn value) {
  begin_ = value;
  CHECK_LE(begin_, end_);
}
void Range::set_begin_line(LineNumber value) {
  begin_.line = value;
  CHECK_LE(begin_, end_);
}
void Range::set_begin_column(ColumnNumber value) {
  begin_.column = value;
  CHECK_LE(begin_, end_);
}

LineColumn Range::end() const { return end_; }
void Range::set_end(LineColumn value) {
  end_ = value;
  CHECK_LE(begin_, end_);
}
void Range::set_end_line(LineNumber value) {
  end_.line = value;
  CHECK_LE(begin_, end_);
}
void Range::set_end_column(ColumnNumber value) {
  end_.column = value;
  CHECK_LE(begin_, end_);
}

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin() << ", " << range.end() << ")";
  return os;
}

bool operator<(const Range& a, const Range& b) {
  return a.begin() < b.begin() || (a.begin() == b.begin() && a.end() < b.end());
}
}  // namespace afc::language::text
