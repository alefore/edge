#include "src/transformation/tree_navigate.h"

#include "src/buffer.h"
#include "src/parse_tree.h"
#include "src/seek.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class TreeNavigate : public CompositeTransformation {
  std::wstring Serialize() const override { return L"TreeNavigate()"; }
  void Apply(Input input) const override {
    auto root = input.buffer->parse_tree();
    if (root == nullptr) return;
    const ParseTree* tree = root.get();
    auto next_position = input.position;
    Seek(*input.buffer->contents(), &next_position).Once();
    while (true) {
      size_t child = 0;
      while (child < tree->children().size() &&
             (tree->children()[child].range().end <= input.position ||
              tree->children()[child].children().empty())) {
        child++;
      }
      if (child < tree->children().size()) {
        bool descend = false;
        auto candidate = &tree->children()[child];
        if (tree->range().begin < input.position) {
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
      Seek(*input.buffer->contents(), &last_position).Backwards().Once();
      input.push(NewSetPositionTransformation(input.position == last_position
                                                  ? tree->range().begin
                                                  : last_position));
      return;
    }
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<TreeNavigate>();
  }
};
}  // namespace
std::unique_ptr<Transformation> NewTreeNavigateTransformation() {
  return NewTransformation(Modifiers(), std::make_unique<TreeNavigate>());
}
}  // namespace afc::editor
