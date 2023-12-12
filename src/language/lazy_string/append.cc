#include "src/language/lazy_string/append.h"

#include <glog/logging.h>

#include "src/language/const_tree.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
class AppendImpl : public LazyStringImpl {
 public:
  using Tree = ConstTree<VectorBlock<wchar_t, 64>, 64>;

  AppendImpl(Tree::Ptr tree) : tree_(std::move(tree)) {}

  wchar_t get(ColumnNumber pos) const { return tree_->Get(pos.read()); }

  ColumnNumberDelta size() const {
    return ColumnNumberDelta(Tree::Size(tree_));
  }

  const Tree::Ptr& tree() const { return tree_; }

  static AppendImpl::Tree::Ptr TreeFrom(LazyString a) {
    if (auto a_cast = dynamic_cast<const AppendImpl*>(a.data_.get().get());
        a_cast != nullptr) {
      return a_cast->tree();
    }
    AppendImpl::Tree::Ptr output;
    ForEachColumn(a, [&output](ColumnNumber, wchar_t c) {
      output = AppendImpl::Tree::PushBack(output, c).get_shared();
    });
    return output;
  }

 private:
  const Tree::Ptr tree_;
};

LazyString Append(LazyString a, LazyString b) {
  if (a.size() == ColumnNumberDelta()) return b;
  if (b.size() == ColumnNumberDelta()) return a;
  return LazyString(MakeNonNullShared<AppendImpl>(AppendImpl::Tree::Append(
      AppendImpl::TreeFrom(a), AppendImpl::TreeFrom(b))));
}

LazyString Append(LazyString a, LazyString b, LazyString c) {
  return Append(std::move(a), Append(std::move(b), std::move(c)));
}

LazyString Append(LazyString a, LazyString b, LazyString c, LazyString d) {
  return Append(Append(std::move(a), std::move(b)),
                Append(std::move(c), std::move(d)));
}
}  // namespace afc::language::lazy_string
