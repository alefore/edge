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

void TransformationStack::PushFront(
    transformation::BaseTransformation transformation) {
  PushFront(transformation::Build(std::move(transformation)));
}

futures::Value<Transformation::Result> TransformationStack::Apply(
    const Input& input) const {
  auto output = std::make_shared<Result>(input.position);
  return futures::Transform(
      futures::ForEach(
          stack_->begin(), stack_->end(),
          [output, input, stack = stack_](
              const std::unique_ptr<Transformation>& transformation) {
            Input sub_input(input.buffer);
            sub_input.position = output->position;
            sub_input.mode = input.mode;
            return futures::Transform(
                transformation->Apply(sub_input),
                [output](Transformation::Result result) {
                  output->MergeFrom(std::move(result));
                  return output->success
                             ? futures::IterationControlCommand::kContinue
                             : futures::IterationControlCommand::kStop;
                });
          }),
      [output](futures::IterationControlCommand) {
        return std::move(*output);
      });
}

std::unique_ptr<Transformation> TransformationStack::Clone() const {
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
  return stack;
}
}  // namespace afc::editor
