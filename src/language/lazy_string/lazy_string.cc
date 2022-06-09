#include "src/language/lazy_string/lazy_string.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"

namespace afc::language::lazy_string {
// TODO(easy, 2022-06-09): Get rid of all `using language::...` declarations in
// files in language/lazy_string.
using language::NonNull;
namespace {
using infrastructure::Tracker;

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
}  // namespace afc::language::lazy_string
