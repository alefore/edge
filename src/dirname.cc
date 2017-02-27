#include "dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include "wstring.h"

namespace afc {
namespace editor {

std::wstring Dirname(std::wstring path) {
  VLOG(5) << "Dirname: " << path;
  std::unique_ptr<char> tmp(strdup(ToByteString(path).c_str()));
  CHECK(tmp != nullptr);
  return FromByteString(dirname(tmp.get()));
}

}  // namespace editor
}  // namespace afc