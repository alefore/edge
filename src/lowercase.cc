#include "src/lowercase.h"

#include <glog/logging.h>

#include <memory>

#include "src/line_column.h"

namespace afc {
namespace editor {

using std::shared_ptr;

namespace {
class LowerCaseImpl : public LazyString {
 public:
  LowerCaseImpl(shared_ptr<LazyString> input) : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return towlower(input_->get(pos));
  }

  ColumnNumberDelta size() const override { return input_->size(); }

 private:
  const shared_ptr<LazyString> input_;
};
}  // namespace

shared_ptr<LazyString> LowerCase(shared_ptr<LazyString> input) {
  DCHECK(input != nullptr);
  return std::make_shared<LowerCaseImpl>(std::move(input));
}

}  // namespace editor
}  // namespace afc
