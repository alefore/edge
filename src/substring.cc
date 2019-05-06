#include "src/substring.h"

#include <glog/logging.h>

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

  size_t size() const override { return (ColumnNumber(0) + delta_).column; }

 private:
  const shared_ptr<LazyString> buffer_;
  // First column to read from.
  const ColumnNumber column_;
  const ColumnNumberDelta delta_;
};

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos) {
  return Substring(input, pos, input->size() - pos);
}

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column) {
  return Substring(input, column, ColumnNumber(input->size()) - column);
}

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos, size_t size) {
  return Substring(input, ColumnNumber(pos), ColumnNumberDelta(size));
}

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column, ColumnNumberDelta delta) {
  CHECK(input != nullptr);
  if (column.IsZero() && delta == ColumnNumberDelta(input->size())) {
    return input;  // Optimization.
  }
  CHECK_LE(column, ColumnNumber(input->size()));
  CHECK_LE(column + delta, ColumnNumber(input->size()));
  return std::make_shared<SubstringImpl>(input, column, delta);
}

}  // namespace editor
}  // namespace afc
