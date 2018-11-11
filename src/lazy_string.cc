#include "lazy_string.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

namespace {
class EmptyStringImpl : public LazyString {
 public:
  wchar_t get(size_t) const {
    LOG(FATAL) << "Attempt to read from empty string.";
  }
  size_t size() const { return 0; }
};
}  // namespace

std::shared_ptr<LazyString> EmptyString() {
  return std::make_shared<EmptyStringImpl>();
}

}  // namespace editor
}  // namespace afc
