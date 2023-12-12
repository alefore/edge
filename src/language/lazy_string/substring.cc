#include "src/language/lazy_string/substring.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::lazy_string {
namespace {
class SubstringImpl : public LazyStringImpl {
 public:
  SubstringImpl(const LazyString input, ColumnNumber column,
                ColumnNumberDelta delta)
      : buffer_(input), column_(column), delta_(delta) {}

  wchar_t get(ColumnNumber pos) const override {
    return buffer_.get(column_ + pos.ToDelta());
  }

  ColumnNumberDelta size() const override { return delta_; }

 private:
  const LazyString buffer_;
  // First column to read from.
  const ColumnNumber column_;
  const ColumnNumberDelta delta_;
};
}  // namespace

LazyString Substring(LazyString input, ColumnNumber column) {
  auto size = input.size();
  return Substring(std::move(input), column, size - column.ToDelta());
}

LazyString Substring(LazyString input, ColumnNumber column,
                     ColumnNumberDelta delta) {
  if (column.IsZero() && delta == ColumnNumberDelta(input.size())) {
    return input;  // Optimization.
  }
  CHECK_GE(delta, ColumnNumberDelta(0));
  CHECK_LE(column, ColumnNumber(0) + input.size());
  CHECK_LE(column + delta, ColumnNumber(0) + input.size());
  return LazyString(
      MakeNonNullShared<SubstringImpl>(std::move(input), column, delta));
}

LazyString SubstringWithRangeChecks(LazyString input, ColumnNumber column,
                                    ColumnNumberDelta delta) {
  auto length = ColumnNumberDelta(input.size());
  column = std::min(column, ColumnNumber(0) + length);
  return Substring(std::move(input), column,
                   std::min(delta, length - column.ToDelta()));
}
}  // namespace afc::language::lazy_string
