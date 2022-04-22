#include "src/lazy_string_trim.h"

#include <glog/logging.h>

#include "src/lazy_string_functional.h"
#include "src/substring.h"

namespace afc {
namespace editor {

std::shared_ptr<LazyString> StringTrimLeft(std::shared_ptr<LazyString> source,
                                           std::wstring space_characters) {
  CHECK(source != nullptr);
  return Substring(source, FindFirstColumnWithPredicate(
                               *source,
                               [&](ColumnNumber, wchar_t c) {
                                 return space_characters.find(c) ==
                                        std::wstring::npos;
                               })
                               .value_or(ColumnNumber(0) + source->size()))
      .get_shared();
}

}  // namespace editor
}  // namespace afc
