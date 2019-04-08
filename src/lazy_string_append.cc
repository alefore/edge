#include "lazy_string_append.h"

#include <glog/logging.h>

namespace afc {
namespace editor {
namespace {
class StringAppendImpl : public LazyString {
 public:
  StringAppendImpl(std::shared_ptr<LazyString> a, std::shared_ptr<LazyString> b)
      : size_(a->size() + b->size()), a_(std::move(a)), b_(std::move(b)) {}

  wchar_t get(size_t pos) const {
    if (pos < a_->size()) {
      return a_->get(pos);
    }
    return b_->get(pos - a_->size());
  }

  size_t size() const { return size_; }

 private:
  const int size_;
  const std::shared_ptr<LazyString> a_;
  const std::shared_ptr<LazyString> b_;
};
}  // namespace

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b) {
  CHECK(a != nullptr);
  CHECK(b != nullptr);
  if (a->size() == 0) {
    return b;
  }
  if (b->size() == 0) {
    return a;
  }
  return std::make_shared<StringAppendImpl>(std::move(a), std::move(b));
}

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b,
                                         std::shared_ptr<LazyString> c) {
  return StringAppend(std::move(a), StringAppend(std::move(b), std::move(c)));
}

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b,
                                         std::shared_ptr<LazyString> c,
                                         std::shared_ptr<LazyString> d) {
  return StringAppend(StringAppend(std::move(a), std::move(b)),
                      StringAppend(std::move(c), std::move(d)));
}

}  // namespace editor
}  // namespace afc
