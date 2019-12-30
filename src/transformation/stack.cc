#include "src/transformation/stack.h"

#include "src/vm_transformation.h"

namespace afc::editor {
void TransformationStack::PushBack(
    std::unique_ptr<Transformation> transformation) {
  stack_.push_back(std::move(transformation));
}

void TransformationStack::PushFront(
    std::unique_ptr<Transformation> transformation) {
  stack_.push_front(std::move(transformation));
}

Transformation::Result TransformationStack::Apply(const Input& input) const {
  Result output(input.position);
  for (auto& it : stack_) {
    Input sub_input(input.buffer);
    sub_input.position = output.position;
    sub_input.mode = input.mode;
    output.MergeFrom(it->Apply(sub_input));
    if (!output.success) break;
  }
  return output;
}

std::unique_ptr<Transformation> TransformationStack::Clone() const {
  auto output = std::make_unique<TransformationStack>();
  for (auto& it : stack_) {
    output->PushBack(it->Clone());
  }
  return std::move(output);
}

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b) {
  auto stack = std::make_unique<TransformationStack>();
  stack->PushBack(std::move(a));
  stack->PushBack(std::move(b));
  return std::move(stack);
}
}  // namespace afc::editor
