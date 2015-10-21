#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <string>

namespace afc {
namespace editor {

// A position in a text buffer.
struct LineColumn {
  LineColumn() : line(0), column(0) {}
  LineColumn(size_t l) : line(l), column(0) {}
  LineColumn(size_t l, size_t c) : line(l), column(c) {}

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

  size_t line;
  size_t column;

  friend std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
};

std::ostream& operator<<(std::ostream& os, const LineColumn& lc);

} // namespace editor
} // namespace afc

#endif
