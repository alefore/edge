#include "src/language/lazy_string/char_buffer.h"

#include <glog/logging.h>

#include <algorithm>
#include <ranges>

#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
namespace {
// TODO(2026-05-16, P2, trivial): Deduplicate with the version in
// lazy_string.cc.
template <typename Container>
class StringFromContainer : public LazyStringImpl {
 public:
  StringFromContainer(Container data) : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos, ColumnNumber(data_.size()));
    return data_.at(pos.read());
  }

  ColumnNumberDelta size() const { return ColumnNumberDelta(data_.size()); }

  bool Every(std::function<bool(wchar_t)> callback, ColumnNumber start,
             ColumnNumberDelta size) const override {
    CHECK_GE(size, ColumnNumberDelta{});
    CHECK_LE((start + size).ToDelta(), this->size());
    return std::ranges::all_of(
        data_ | std::views::drop(start.read()) | std::views::take(size.read()),
        callback);
  }

 protected:
  const Container data_;
};
}  // namespace

LazyString NewLazyString(std::vector<wchar_t> data) {
  return LazyString(
      MakeNonNullShared<StringFromContainer<std::vector<wchar_t>>>(
          std::move(data)));
}

}  // namespace afc::language::lazy_string
