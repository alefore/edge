#include "src/language/lazy_string/lazy_string.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"

namespace afc::language::lazy_string {
namespace {
using infrastructure::Tracker;
using ::operator<<;

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
  std::wstring output(size().read(), 0);
  ForEachColumn(*this,
                [&output](ColumnNumber i, wchar_t c) { output[i.read()] = c; });
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

NonNull<std::shared_ptr<LazyString>> EmptyString() {
  return NonNull<std::shared_ptr<EmptyStringImpl>>();
}

bool operator==(const LazyString& a, const LazyString& b) {
  return a.size() == b.size() &&
         !FindFirstColumnWithPredicate(a, [&](ColumnNumber column, wchar_t c) {
            return b.get(column) != c;
          }).has_value();
}

std::ostream& operator<<(std::ostream& os,
                         const afc::language::lazy_string::LazyString& obj) {
  // TODO(P2): Find another way to implement this.
  os << obj.ToString();
  return os;
}

}  // namespace afc::language::lazy_string
