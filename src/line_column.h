#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

// A position in a text buffer.
struct LineColumn {
  static void Register(vm::Environment* environment);

  LineColumn() {}
  LineColumn(std::vector<int> pos)
      : line(pos.size() > 0 ? pos[0] : 0),
        column(pos.size() > 1 ? pos[1] : 0) {}
  LineColumn(size_t l) : LineColumn(l, 0) {}
  LineColumn(size_t l, size_t c) : line(l), column(c) {}

  static LineColumn Max() {
    return LineColumn(std::numeric_limits<size_t>::max(),
                      std::numeric_limits<size_t>::max());
  }

  bool operator!=(const LineColumn& other) const;

  bool at_beginning_of_line() const { return column == 0; }
  bool at_beginning() const { return line == 0 && at_beginning_of_line(); }

  std::wstring ToString() const {
    return std::to_wstring(line) + L" " + std::to_wstring(column);
  }

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

  std::wstring ToCppString() const;

  size_t line = 0;
  size_t column = 0;

  friend std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
};

std::ostream& operator<<(std::ostream& os, const LineColumn& lc);

struct Range {
  static void Register(vm::Environment* environment);

  Range() = default;
  Range(LineColumn begin, LineColumn end) : begin(begin), end(end) {}

  static Range InLine(size_t line, size_t column, size_t size) {
    return Range(LineColumn(line, column), LineColumn(line, column + size));
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

  Range Intersection(const Range& other) const {
    if (Disjoint(other)) {
      return Range();
    }
    return Range(std::max(begin, other.begin), std::min(end, other.end));
  }

  bool operator==(const Range& rhs) const {
    return begin == rhs.begin && end == rhs.end;
  }

  LineColumn begin;
  LineColumn end;
};

std::ostream& operator<<(std::ostream& os, const Range& range);

}  // namespace editor
}  // namespace afc

#endif
