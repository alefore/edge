#include "src/lazy_string.h"

#include <glog/logging.h>

#include "src/lazy_string_functional.h"

namespace afc {
namespace editor {

namespace {
class EmptyStringImpl : public LazyString {
 public:
  wchar_t get(ColumnNumber) const override {
    LOG(FATAL) << "Attempt to read from empty string.";
  }
  size_t size() const override { return 0; }
};
}  // namespace

wstring LazyString::ToString() const {
  wstring output(size(), 0);
  ForEachColumn(*this,
                [&output](ColumnNumber i, wchar_t c) { output[i.column] = c; });
  return output;
}

bool LazyString::operator<(const LazyString& x) {
  for (ColumnNumber current; current < ColumnNumber(size()); ++current) {
    if (current == ColumnNumber(x.size())) {
      return false;
    }
    if (get(current) < x.get(current)) {
      return true;
    }
    if (get(current) > x.get(current)) {
      return false;
    }
  }
  return size() < x.size();
}

std::shared_ptr<LazyString> EmptyString() {
  return std::make_shared<EmptyStringImpl>();
}

}  // namespace editor
}  // namespace afc
