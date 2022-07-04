#include "src/language/lazy_string/append.h"

#include <glog/logging.h>

#include "src/language/const_tree.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
namespace {
class AppendImpl : public LazyString {
 public:
  using Tree = ConstTree<VectorBlock<wchar_t, 64>, 64>;

  AppendImpl(Tree::Ptr tree) : tree_(std::move(tree)) {}

  wchar_t get(ColumnNumber pos) const { return tree_->Get(pos.read()); }

  ColumnNumberDelta size() const {
    return ColumnNumberDelta(Tree::Size(tree_));
  }

  const Tree::Ptr& tree() const { return tree_; }

 private:
  const Tree::Ptr tree_;
};

AppendImpl::Tree::Ptr TreeFrom(NonNull<std::shared_ptr<LazyString>> a) {
  auto a_cast = dynamic_cast<AppendImpl*>(a.get().get());
  if (a_cast != nullptr) {
    return a_cast->tree();
  }
  AppendImpl::Tree::Ptr output;
  ForEachColumn(a.value(), [&output](ColumnNumber, wchar_t c) {
    output = AppendImpl::Tree::PushBack(output, c);
  });
  return output;
}
}  // namespace

NonNull<std::shared_ptr<LazyString>> Append(
    NonNull<std::shared_ptr<LazyString>> a,
    NonNull<std::shared_ptr<LazyString>> b) {
  if (a->size() == ColumnNumberDelta()) {
    return b;
  }
  if (b->size() == ColumnNumberDelta()) {
    return a;
  }

  return MakeNonNullShared<AppendImpl>(
      AppendImpl::Tree::Append(TreeFrom(a), TreeFrom(b)));
}

NonNull<std::shared_ptr<LazyString>> Append(
    NonNull<std::shared_ptr<LazyString>> a,
    NonNull<std::shared_ptr<LazyString>> b,
    NonNull<std::shared_ptr<LazyString>> c) {
  return Append(std::move(a), Append(std::move(b), std::move(c)));
}

NonNull<std::shared_ptr<LazyString>> Append(
    NonNull<std::shared_ptr<LazyString>> a,
    NonNull<std::shared_ptr<LazyString>> b,
    NonNull<std::shared_ptr<LazyString>> c,
    NonNull<std::shared_ptr<LazyString>> d) {
  return Append(Append(std::move(a), std::move(b)),
                Append(std::move(c), std::move(d)));
}

NonNull<std::shared_ptr<LazyString>> Concatenate(
    std::vector<NonNull<std::shared_ptr<LazyString>>> inputs) {
  // TODO: There's probably a faster way to do this. Not sure it matters.
  NonNull<std::shared_ptr<LazyString>> output = EmptyString();
  for (auto& i : inputs) {
    output = Append(std::move(output), i);
  }
  return output;
}

}  // namespace afc::language::lazy_string
