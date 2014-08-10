#include "lazy_string_append.h"

namespace {

using namespace afc::editor;

class StringAppendImpl : public LazyString {
 public:
  StringAppendImpl(const shared_ptr<LazyString>& a,
                   const shared_ptr<LazyString>& b)
      : a_(a), b_(b) {}

  char get(size_t pos) const {
    if (pos < a_->size()) {
      return a_->get(pos);
    }
    return b_->get(pos - a_->size());
  }

  size_t size() const {
    return a_->size() + b_->size();
  }

 private:
  const shared_ptr<LazyString> a_;
  const shared_ptr<LazyString> b_;
};

}  // namespace

namespace afc {
namespace editor {

shared_ptr<LazyString> StringAppend(const shared_ptr<LazyString>& a,
                                    const shared_ptr<LazyString>& b) {
  if (a->size() == 0) { return b; }
  if (b->size() == 0) { return a; }
  shared_ptr<LazyString> output(new StringAppendImpl(a, b));
  return output;
}

}  // namespace editor
}  // namespace afc
