#include "src/lazy_string_trim.h"

#include <glog/logging.h>

#include "src/substring.h"

namespace afc {
namespace editor {

std::shared_ptr<LazyString> StringTrimLeft(std::shared_ptr<LazyString> source,
                                           wstring space_characters) {
  CHECK(source != nullptr);
  size_t pos = 0;
  while (pos < source->size() &&
         space_characters.find(source->get(pos)) != wstring::npos) {
    pos++;
  }
  return Substring(source, pos);
}

}  // namespace editor
}  // namespace afc
