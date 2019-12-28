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

void TransformationStack::Apply(const Input& input, Result* result) const {
  CHECK(result != nullptr);
  for (auto& it : stack_) {
    Result it_result(result->buffer);
    it_result.delete_buffer = result->delete_buffer;
    it_result.cursor = result->cursor;
    it->Apply(input, &it_result);
    result->cursor = it_result.cursor;
    if (it_result.modified_buffer) {
      result->modified_buffer = true;
    }
    if (it_result.made_progress) {
      result->made_progress = true;
    }
    result->undo_stack->PushFront(std::move(it_result.undo_stack));
    if (!it_result.success) {
      result->success = false;
      break;
    }
  }
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
