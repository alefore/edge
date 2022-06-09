#include "src/language/lazy_string/append.h"

#include <glog/logging.h>

#include "src/const_tree.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// TODO(easy, 2022-06-09): GEt rid of this `using` declaration.
using afc::editor::ConstTree;
namespace {
class AppendImpl : public LazyString {
 public:
  AppendImpl(ConstTree<wchar_t>::Ptr tree) : tree_(std::move(tree)) {}

  wchar_t get(ColumnNumber pos) const { return tree_->Get(pos.read()); }

  ColumnNumberDelta size() const {
    return ColumnNumberDelta(ConstTree<wchar_t>::Size(tree_));
  }

  const ConstTree<wchar_t>::Ptr& tree() const { return tree_; }

 private:
  const ConstTree<wchar_t>::Ptr tree_;
};

ConstTree<wchar_t>::Ptr TreeFrom(NonNull<std::shared_ptr<LazyString>> a) {
  auto a_cast = dynamic_cast<AppendImpl*>(a.get().get());
  if (a_cast != nullptr) {
    return a_cast->tree();
  }
  ConstTree<wchar_t>::Ptr output;
  ForEachColumn(a.value(), [&output](ColumnNumber, wchar_t c) {
    output = ConstTree<wchar_t>::PushBack(output, c);
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
      ConstTree<wchar_t>::Append(TreeFrom(a), TreeFrom(b)));
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
