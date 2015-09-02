#include "line_column.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const LineColumn& lc) {
    os << "[" << lc.line << ":" << lc.column << "]";
    return os;
}

bool LineColumn::operator!=(const LineColumn& other) const {
  return line != other.line || column != other.column;
}

} // namespace editor
} // namespace afc
