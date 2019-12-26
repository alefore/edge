#include "src/transformation/tree_navigate.h"

#include "src/buffer.h"
#include "src/parse_tree.h"
#include "src/seek.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class TreeNavigate : public Transformation {
  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    auto root = result->buffer->parse_tree();
    if (root == nullptr) {
      result->success = false;
      return;
    }
    const ParseTree* tree = root.get();
    auto next_position = result->cursor;
    Seek(*result->buffer->contents(), &next_position).Once();
    while (true) {
      size_t child = 0;
      while (child < tree->children().size() &&
             (tree->children()[child].range().end <= result->cursor ||
              tree->children()[child].children().empty())) {
        child++;
      }
      if (child < tree->children().size()) {
        bool descend = false;
        auto candidate = &tree->children()[child];
        if (tree->range().begin < result->cursor) {
          descend = true;
        } else if (tree->range().end == next_position) {
          descend = candidate->range().end == next_position;
        }

        if (descend) {
          tree = candidate;
          continue;
        }
      }

      auto last_position = tree->range().end;
      Seek(*result->buffer->contents(), &last_position).Backwards().Once();

      auto original_cursor = result->cursor;
      result->cursor = result->cursor < tree->range().begin ||
                               result->cursor == last_position
                           ? tree->range().begin
                           : last_position;
      result->success = original_cursor != result->cursor;
      return;
    }
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<TreeNavigate>();
  }
};
}  // namespace
std::unique_ptr<Transformation> NewTreeNavigateTransformation() {
  return std::make_unique<TreeNavigate>();
}
}  // namespace afc::editor
