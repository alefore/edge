#include "src/transformation/stack.h"

#include "src/vm_transformation.h"

namespace afc::editor {
TransformationStack::TransformationStack()
    : stack_(std::make_shared<std::list<std::unique_ptr<Transformation>>>()) {}

void TransformationStack::PushBack(
    std::unique_ptr<Transformation> transformation) {
  stack_->push_back(std::move(transformation));
}

void TransformationStack::PushFront(
    std::unique_ptr<Transformation> transformation) {
  stack_->push_front(std::move(transformation));
}

DelayedValue<Transformation::Result> TransformationStack::Apply(
    const Input& input) const {
  auto output = std::make_shared<Result>(input.position);
  return DelayedValue<Transformation::Result>::Transform(
      futures::ForEach(
          stack_->begin(), stack_->end(),
          [output, input, stack = stack_](
              const std::unique_ptr<Transformation>& transformation) {
            Input sub_input(input.buffer);
            sub_input.position = output->position;
            sub_input.mode = input.mode;
            return DelayedValue<futures::IterationControlCommand>::Transform(
                transformation->Apply(sub_input),
                [output](const Transformation::Result& result) {
                  output->MergeFrom(result);
                  return futures::ImmediateValue(
                      output->success
                          ? futures::IterationControlCommand::kContinue
                          : futures::IterationControlCommand::kStop);
                });
          }),
      [output](futures::IterationControlCommand) {
        return futures::ImmediateValue(std::move(*output));
      });
}

std::unique_ptr<Transformation> TransformationStack::Clone() const {
  return CloneStack();
}

std::unique_ptr<TransformationStack> TransformationStack::CloneStack() const {
  auto output = std::make_unique<TransformationStack>();
  for (auto& it : *stack_) {
    output->PushBack(it->Clone());
  }
  return output;
}

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b) {
  auto stack = std::make_unique<TransformationStack>();
  stack->PushBack(std::move(a));
  stack->PushBack(std::move(b));
  return std::move(stack);
}
}  // namespace afc::editor
