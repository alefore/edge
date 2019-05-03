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

struct LineNumberDelta {
  LineNumberDelta() = default;
  explicit LineNumberDelta(int value) : line_delta(value) {}

  int line_delta = 0;
};

bool operator==(const LineNumberDelta& a, const LineNumberDelta& b);
std::ostream& operator<<(std::ostream& os, const LineNumberDelta& lc);
bool operator<(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator<=(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator>(const LineNumberDelta& a, const LineNumberDelta& b);
bool operator>=(const LineNumberDelta& a, const LineNumberDelta& b);
LineNumberDelta operator+(LineNumberDelta a, const LineNumberDelta& b);
LineNumberDelta operator-(LineNumberDelta a, const LineNumberDelta& b);
LineNumberDelta operator-(LineNumberDelta a);
LineNumberDelta operator*(LineNumberDelta a, const size_t& b);
LineNumberDelta operator*(const size_t& a, LineNumberDelta b);
LineNumberDelta operator/(LineNumberDelta a, const size_t& b);
LineNumberDelta& operator+=(LineNumberDelta& a, const LineNumberDelta& value);
LineNumberDelta& operator-=(LineNumberDelta& a, const LineNumberDelta& value);
LineNumberDelta& operator++(LineNumberDelta& a);
LineNumberDelta operator++(LineNumberDelta& a, int);
LineNumberDelta& operator--(LineNumberDelta& a);
LineNumberDelta operator--(LineNumberDelta& a, int);

struct ColumnNumberDelta {
  // Generates a string of the length specified by `this` filled up with the
  // character given.
  //
  // If length is negative (or zero), returns an empty string.
  static std::wstring PaddingString(const ColumnNumberDelta& length,
                                    wchar_t fill);

  ColumnNumberDelta() = default;
  explicit ColumnNumberDelta(int value) : column_delta(value) {}

  ColumnNumberDelta& operator=(const ColumnNumberDelta& delta);

  int column_delta = 0;
};

bool operator==(const ColumnNumberDelta& a, const ColumnNumberDelta& b);
std::ostream& operator<<(std::ostream& os, const ColumnNumberDelta& lc);
bool operator<(const ColumnNumberDelta& a, const ColumnNumberDelta& b);
bool operator<=(const ColumnNumberDelta& a, const ColumnNumberDelta& b);
bool operator>(const ColumnNumberDelta& a, const ColumnNumberDelta& b);
bool operator>=(const ColumnNumberDelta& a, const ColumnNumberDelta& b);
ColumnNumberDelta operator+(ColumnNumberDelta a, const ColumnNumberDelta& b);
ColumnNumberDelta operator-(ColumnNumberDelta a, const ColumnNumberDelta& b);
ColumnNumberDelta operator-(ColumnNumberDelta a);
ColumnNumberDelta operator*(ColumnNumberDelta a, const size_t& b);
ColumnNumberDelta operator*(const size_t& a, ColumnNumberDelta b);
ColumnNumberDelta operator/(ColumnNumberDelta a, const size_t& b);
ColumnNumberDelta& operator+=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value);
ColumnNumberDelta& operator-=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value);
ColumnNumberDelta& operator++(ColumnNumberDelta& a);
ColumnNumberDelta operator++(ColumnNumberDelta& a, int);
ColumnNumberDelta& operator--(ColumnNumberDelta& a);
ColumnNumberDelta operator--(ColumnNumberDelta& a, int);

// First adds the line, then adds the column.
struct LineColumnDelta {
  LineNumberDelta line;
  ColumnNumberDelta column;
};

bool operator==(const LineColumnDelta& a, const LineColumnDelta& b);
bool operator!=(const LineColumnDelta& a, const LineColumnDelta& b);

struct LineNumber {
  LineNumber() = default;
  explicit LineNumber(size_t column);

  LineNumber& operator=(const LineNumber& value);

  LineNumberDelta ToDelta() const;

  // Starts counting from 1 (i.e. LineNumber(5).ToUserString() evaluates to
  // L"6").
  std::wstring ToUserString() const;

  LineNumber next() const;
  LineNumber previous() const;

  size_t line = 0;
};

bool operator==(const LineNumber& a, const LineNumber& b);
bool operator!=(const LineNumber& a, const LineNumber& b);
bool operator<(const LineNumber& a, const LineNumber& b);
bool operator<=(const LineNumber& a, const LineNumber& b);
bool operator>(const LineNumber& a, const LineNumber& b);
bool operator>=(const LineNumber& a, const LineNumber& b);
LineNumber& operator+=(LineNumber& a, const LineNumberDelta& delta);
LineNumber& operator-=(LineNumber& a, const LineNumberDelta& delta);
LineNumber& operator++(LineNumber& a);
LineNumber operator++(LineNumber& a, int);
LineNumber& operator--(LineNumber& a);
LineNumber operator--(LineNumber& a, int);
LineNumber operator+(LineNumber a, const LineNumberDelta& delta);
LineNumber operator-(LineNumber a, const LineNumberDelta& delta);
LineNumber operator-(LineNumber a);
LineNumberDelta operator-(const LineNumber& a, const LineNumber& b);
std::ostream& operator<<(std::ostream& os, const LineNumber& lc);

struct ColumnNumber {
  ColumnNumber() = default;
  explicit ColumnNumber(size_t column);

  ColumnNumber& operator=(const ColumnNumber& value);

  ColumnNumberDelta ToDelta() const;

  // Starts counting from 1 (i.e. ColumnNumber(5).ToUserString() evaluates to
  // L"6").
  std::wstring ToUserString() const;

  size_t column = 0;
};

bool operator==(const ColumnNumber& a, const ColumnNumber& b);
bool operator!=(const ColumnNumber& a, const ColumnNumber& b);
bool operator<(const ColumnNumber& a, const ColumnNumber& b);
bool operator<=(const ColumnNumber& a, const ColumnNumber& b);
bool operator>(const ColumnNumber& a, const ColumnNumber& b);
bool operator>=(const ColumnNumber& a, const ColumnNumber& b);
ColumnNumber& operator+=(ColumnNumber& a, const ColumnNumberDelta& delta);
ColumnNumber& operator++(ColumnNumber& a);
ColumnNumber operator++(ColumnNumber& a, int);
ColumnNumber& operator--(ColumnNumber& a);
ColumnNumber operator--(ColumnNumber& a, int);
ColumnNumber operator+(ColumnNumber a, const ColumnNumberDelta& delta);
ColumnNumber operator-(ColumnNumber a, const ColumnNumberDelta& delta);
ColumnNumber operator-(ColumnNumber a);
ColumnNumberDelta operator-(const ColumnNumber& a, const ColumnNumber& b);
std::ostream& operator<<(std::ostream& os, const ColumnNumber& lc);

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
namespace afc {
namespace editor {
// A position in a text buffer.
struct LineColumn {
  static void Register(vm::Environment* environment);

  LineColumn() = default;
  explicit LineColumn(LineNumber l) : LineColumn(l, ColumnNumber(0)) {}
  LineColumn(LineNumber l, ColumnNumber c) : line(l), column(c) {}

  static LineColumn Max() {
    return LineColumn(std::numeric_limits<LineNumber>::max(),
                      std::numeric_limits<ColumnNumber>::max());
  }

  bool operator!=(const LineColumn& other) const;

  std::wstring ToString() const;

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

// A range that contains every position i such that begin <= i < end.
struct Range {
  static void Register(vm::Environment* environment);

  Range() = default;
  Range(LineColumn begin, LineColumn end) : begin(begin), end(end) {}

  static Range InLine(LineNumber line, ColumnNumber column,
                      ColumnNumberDelta size);

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

  LineNumberDelta lines() const {
    return end.line - begin.line + LineNumberDelta(1);
  }

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
