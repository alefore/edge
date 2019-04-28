#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

struct LinesDelta {
  LinesDelta() = default;
  explicit LinesDelta(int delta) : delta(delta) {}
  int delta = 0;
};

struct ColumnsDelta {
  ColumnsDelta() = default;
  explicit ColumnsDelta(int delta) : delta(delta) {}
  int delta = 0;
};

// A position in a text buffer.
struct LineColumn {
  static void Register(vm::Environment* environment);

  LineColumn() {}
  // TODO: Make single-argument constructors explicit.
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

  LineColumn operator+(const LinesDelta& delta) const;
  LineColumn operator-(const LinesDelta& delta) const;
  LineColumn& operator+=(const LinesDelta& delta);
  LineColumn& operator-=(const LinesDelta& delta);

  LineColumn operator+(const ColumnsDelta& delta) const;
  LineColumn operator-(const ColumnsDelta& delta) const;

  std::wstring ToCppString() const;

  size_t line = 0;
  size_t column = 0;

  friend std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
};

std::ostream& operator<<(std::ostream& os, const LineColumn& lc);

// A range that contains every position i such that begin <= i < end.
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

  size_t lines() const { return end.line - begin.line + 1; }

  LineColumn begin;
  LineColumn end;
};

std::ostream& operator<<(std::ostream& os, const Range& range);

}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::LineColumn> {
  static editor::LineColumn get(Value* value);
  static Value::Ptr New(editor::LineColumn value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif
