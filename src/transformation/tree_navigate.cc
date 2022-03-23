#include "src/transformation/tree_navigate.h"

#include "src/buffer.h"
#include "src/futures/futures.h"
#include "src/parse_tree.h"
#include "src/seek.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"
#include "src/vm_transformation.h"

namespace afc::editor {
std::wstring TreeNavigate::Serialize() const { return L"TreeNavigate()"; }

futures::Value<CompositeTransformation::Output> TreeNavigate::Apply(
    Input input) const {
  auto root = input.buffer->parse_tree();
  if (root == nullptr) return futures::Past(Output());
  const ParseTree* tree = root.get();
  auto next_position = input.position;
  Seek(input.buffer->contents(), &next_position).Once();

  while (true) {
    // Find the first relevant child at the current level.
    size_t child = 0;
    while (child < tree->children().size() &&
           (tree->children()[child].range().end <= input.position ||
            tree->children()[child].children().empty())) {
      child++;
    }

    if (child >= tree->children().size()) {
      break;
    }

    auto candidate = &tree->children()[child];
    if (tree->range().begin >= input.position &&
        (tree->range().end != next_position ||
         candidate->range().end != next_position)) {
      break;
    }
    tree = candidate;
  }

  auto last_position = tree->range().end;
  Seek(input.buffer->contents(), &last_position).Backwards().Once();
  return futures::Past(Output::SetPosition(
      input.position == last_position ? tree->range().begin : last_position));
}

std::unique_ptr<CompositeTransformation> TreeNavigate::Clone() const {
  return std::make_unique<TreeNavigate>();
}
}  // namespace afc::editor
