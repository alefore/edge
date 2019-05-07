#include "src/substring.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/line_column.h"

namespace afc {
namespace editor {

class SubstringImpl : public LazyString {
 public:
  SubstringImpl(const shared_ptr<LazyString>& input, ColumnNumber column,
                ColumnNumberDelta delta)
      : buffer_(input), column_(column), delta_(delta) {}

  wchar_t get(ColumnNumber pos) const override {
    return buffer_->get(column_ + pos.ToDelta());
  }

  ColumnNumberDelta size() const override { return delta_; }

 private:
  const shared_ptr<LazyString> buffer_;
  // First column to read from.
  const ColumnNumber column_;
  const ColumnNumberDelta delta_;
};

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column) {
  return Substring(input, column, input->size() - column.ToDelta());
}

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column, ColumnNumberDelta delta) {
  CHECK(input != nullptr);
  if (column.IsZero() && delta == ColumnNumberDelta(input->size())) {
    return input;  // Optimization.
  }
  CHECK_LE(column, ColumnNumber(0) + input->size());
  CHECK_LE(column + delta, ColumnNumber(0) + input->size());
  return std::make_shared<SubstringImpl>(input, column, delta);
}

std::shared_ptr<LazyString> SubstringWithRangeChecks(
    const shared_ptr<LazyString>& input, ColumnNumber column,
    ColumnNumberDelta delta) {
  auto length = ColumnNumberDelta(input->size());
  column = std::min(column, ColumnNumber(0) + length);
  return Substring(std::move(input), column,
                   std::min(delta, length - column.ToDelta()));
}

}  // namespace editor
}  // namespace afc
