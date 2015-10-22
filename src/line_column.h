#ifndef __AFC_EDITOR_LINE_COLUMN_H__
#define __AFC_EDITOR_LINE_COLUMN_H__

#include <iostream>
#include <string>
#include <vector>

namespace afc {
namespace editor {

// A position in a text buffer.
struct LineColumn {
  LineColumn() : LineColumn(0, 0) {}
  LineColumn(std::vector<int> pos)
      : line(pos.size() > 0 ? pos[0] : 0),
        column(pos.size() > 1 ? pos[1] : 0) {}
  LineColumn(size_t l) : LineColumn(l, 0) {}
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

  size_t line;
  size_t column;

  friend std::ostream& operator<<(std::ostream& os, const LineColumn& lc);
};

std::ostream& operator<<(std::ostream& os, const LineColumn& lc);

} // namespace editor
} // namespace afc

#endif
