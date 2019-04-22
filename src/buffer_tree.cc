#include "src/buffer_tree.h"

#include <cctype>
#include <ostream>

#include "src/wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const BufferTree& lc) {
  os << lc.ToString();
  return os;
}

}  // namespace editor
}  // namespace afc
