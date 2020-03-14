#include "src/transformation/stack.h"

#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class Impl : public Transformation {
 public:
  Impl(std::shared_ptr<const std::list<std::unique_ptr<Transformation>>> stack)
      : stack_(std::move(stack)) {}

  futures::Value<Transformation::Result> Apply(const Input& input) const {
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

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<Impl>(stack_);
  }

 private:
  // We use a shared_ptr so that a TransformationStack can be deleted while the
  // evaluation of `Apply` is still running.
  const std::shared_ptr<const std::list<std::unique_ptr<Transformation>>>
      stack_;
};
}  // namespace

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

std::unique_ptr<Transformation> TransformationStack::Build() {
  auto stack = std::make_shared<std::list<std::unique_ptr<Transformation>>>();
  for (auto& it : *stack_) {
    stack->emplace_back(it->Clone());
  }
  return std::make_unique<Impl>(std::move(stack));
}

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b) {
  auto stack = std::make_unique<TransformationStack>();
  stack->PushBack(std::move(a));
  stack->PushBack(std::move(b));
  return stack->Build();
}
}  // namespace afc::editor
