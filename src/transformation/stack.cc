#include "src/transformation/stack.h"

#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace transformation {
futures::Value<Transformation::Result> ApplyBase(const Stack& parameters,
                                                 Transformation::Input input) {
  auto output = std::make_shared<Transformation::Result>(input.position);
  // TODO: Avoid this copy? Ugh.
  auto copy = std::make_shared<std::list<std::unique_ptr<Transformation>>>();
  for (auto& p : parameters.stack) {
    CHECK(p != nullptr);
    copy->push_back(p->Clone());
    CHECK(copy->back() != nullptr);
  }
  return futures::Transform(
      futures::ForEach(
          copy->begin(), copy->end(),
          [output,
           input](const std::unique_ptr<Transformation>& transformation) {
            CHECK(transformation != nullptr);
            Transformation::Input sub_input(input.buffer);
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
      [output, copy](futures::IterationControlCommand) {
        return std::move(*output);
      });
}

void Stack::PushBack(std::unique_ptr<Transformation> transformation) {
  stack.push_back(std::move(transformation));
}

void Stack::PushFront(std::unique_ptr<Transformation> transformation) {
  stack.push_front(std::move(transformation));
}

void Stack::PushFront(transformation::BaseTransformation transformation) {
  PushFront(transformation::Build(std::move(transformation)));
}
}  // namespace transformation

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b) {
  transformation::Stack stack;
  stack.PushBack(std::move(a));
  stack.PushBack(std::move(b));
  return Build(std::move(stack));
}
}  // namespace afc::editor
