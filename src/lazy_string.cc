#include "src/lazy_string.h"

#include <glog/logging.h>

#include "src/lazy_string_functional.h"
#include "src/tracker.h"

namespace afc {
namespace editor {

namespace {
class EmptyStringImpl : public LazyString {
 public:
  wchar_t get(ColumnNumber) const override {
    LOG(FATAL) << "Attempt to read from empty string.";
    return 0;
  }
  ColumnNumberDelta size() const override { return ColumnNumberDelta(0); }
};
}  // namespace

std::wstring LazyString::ToString() const {
  static Tracker tracker(L"LazyString::ToString");
  auto call = tracker.Call();
  std::wstring output(size().column_delta, 0);
  ForEachColumn(*this,
                [&output](ColumnNumber i, wchar_t c) { output[i.column] = c; });
  return output;
}

bool LazyString::operator<(const LazyString& x) {
  for (ColumnNumber current; current.ToDelta() < size(); ++current) {
    if (current.ToDelta() == x.size()) {
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
