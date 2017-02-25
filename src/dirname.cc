#include "dirname.h"

#include <cstring>

extern "C" {
#include <libgen.h>
}

#include "wstring.h"

namespace afc {
namespace editor {

std::wstring Dirname(std::wstring path) {
  std::unique_ptr<char> tmp(strdup(ToByteString(path).c_str()));
  return FromByteString(dirname(tmp.get()));
}

}  // namespace editor
}  // namespace afc