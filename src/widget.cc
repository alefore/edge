#include "src/widget.h"

#include <cctype>
#include <ostream>

#include "src/wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const Widget& lc) {
  os << lc.ToString();
  return os;
}

}  // namespace editor
}  // namespace afc
