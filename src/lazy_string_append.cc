#include "src/lazy_string_append.h"

#include <glog/logging.h>

#include "src/const_tree.h"
#include "src/lazy_string_functional.h"
#include "src/line_column.h"

namespace afc {
namespace editor {
namespace {
class StringAppendImpl : public LazyString {
 public:
  StringAppendImpl(ConstTree<wchar_t>::Ptr tree) : tree_(std::move(tree)) {}

  wchar_t get(ColumnNumber pos) const { return tree_->Get(pos.column); }

  ColumnNumberDelta size() const {
    return ColumnNumberDelta(ConstTree<wchar_t>::Size(tree_));
  }

  const ConstTree<wchar_t>::Ptr& tree() const { return tree_; }

 private:
  const ConstTree<wchar_t>::Ptr tree_;
};

ConstTree<wchar_t>::Ptr TreeFrom(std::shared_ptr<LazyString> a) {
  auto a_cast = dynamic_cast<StringAppendImpl*>(a.get());
  if (a_cast != nullptr) {
    return a_cast->tree();
  }
  ConstTree<wchar_t>::Ptr output;
  ForEachColumn(*a, [&output](ColumnNumber, wchar_t c) {
    output = ConstTree<wchar_t>::PushBack(output, c);
  });
  return output;
}
}  // namespace

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b) {
  CHECK(a != nullptr);
  CHECK(b != nullptr);

  if (a->size() == ColumnNumberDelta()) {
    return b;
  }
  if (b->size() == ColumnNumberDelta()) {
    return a;
  }

  return std::make_shared<StringAppendImpl>(
      ConstTree<wchar_t>::Append(TreeFrom(a), TreeFrom(b)));
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
