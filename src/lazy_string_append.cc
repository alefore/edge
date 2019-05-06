#include "src/lazy_string_append.h"

#include <glog/logging.h>

#include "src/lazy_string_functional.h"
#include "src/line_column.h"
#include "src/tree.h"

namespace afc {
namespace editor {
namespace {
class StringAppendImpl : public LazyString {
 public:
  StringAppendImpl(Tree<wchar_t> tree) : tree_(tree) {}

  wchar_t get(ColumnNumber pos) const { return tree_.at(pos.column); }

  ColumnNumberDelta size() const { return ColumnNumberDelta(tree_.size()); }

  const Tree<wchar_t>& tree() const { return tree_; }

 private:
  const Tree<wchar_t> tree_;
};

void InsertToTree(LazyString* source, Tree<wchar_t>* tree,
                  Tree<wchar_t>::iterator position) {
  ForEachColumn(*source,
                [&](ColumnNumber, wchar_t c) { tree->insert(position, c); });
}
}  // namespace

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b) {
  CHECK(a != nullptr);
  CHECK(b != nullptr);

  if (a->size() == ColumnNumberDelta()) {
    return std::move(b);
  }
  if (b->size() == ColumnNumberDelta()) {
    return std::move(a);
  }

  auto a_cast = dynamic_cast<StringAppendImpl*>(a.get());
  auto b_cast = dynamic_cast<StringAppendImpl*>(b.get());
  Tree<wchar_t> tree;
  if (a_cast != nullptr && (b_cast == nullptr || b->size() <= a->size())) {
    tree = a_cast->tree();
    InsertToTree(b.get(), &tree, tree.end());
    CHECK_EQ(a->size() + b->size(), ColumnNumberDelta(tree.size()));
  } else if (b_cast != nullptr &&
             (a_cast == nullptr || a->size() <= b->size())) {
    tree = b_cast->tree();
    InsertToTree(a.get(), &tree, tree.begin());
    CHECK_EQ(a->size() + b->size(), ColumnNumberDelta(tree.size()));
  } else {
    InsertToTree(a.get(), &tree, tree.end());
    InsertToTree(b.get(), &tree, tree.end());
    CHECK_EQ(a->size() + b->size(), ColumnNumberDelta(tree.size()));
  }

  return std::make_shared<StringAppendImpl>(tree);
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
