#include "lazy_string_append.h"

#include <cassert>

namespace {

using namespace afc::editor;

class StringAppendImpl : public LazyString {
 public:
  StringAppendImpl(const shared_ptr<LazyString>& a,
                   const shared_ptr<LazyString>& b)
      : size_(a->size() + b->size()), a_(a), b_(b) {}

  wchar_t get(size_t pos) const {
    if (pos < a_->size()) {
      return a_->get(pos);
    }
    return b_->get(pos - a_->size());
  }

  size_t size() const { return size_; }

 private:
  const int size_;
  const shared_ptr<LazyString> a_;
  const shared_ptr<LazyString> b_;
};

}  // namespace

namespace afc {
namespace editor {

shared_ptr<LazyString> StringAppend(const shared_ptr<LazyString>& a,
                                    const shared_ptr<LazyString>& b) {
  assert(a.get() != nullptr);
  assert(b.get() != nullptr);
  if (a->size() == 0) { return b; }
  if (b->size() == 0) { return a; }
  return std::make_shared<StringAppendImpl>(a, b);
}

}  // namespace editor
}  // namespace afc
