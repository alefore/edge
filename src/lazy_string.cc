#include "lazy_string.h"

#include <cassert>

namespace afc {
namespace editor {

namespace {
class EmptyStringImpl : public LazyString {
 public:
  wchar_t get(size_t) const { assert(false); }
  size_t size() const { return 0; }
};
}  // namespace

std::shared_ptr<LazyString> EmptyString() {
  return std::make_shared<EmptyStringImpl>();
}

}  // namespace editor
}  // namespace afc
