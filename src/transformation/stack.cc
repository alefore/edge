#include "src/transformation/stack.h"

#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace transformation {
futures::Value<Result> ApplyBase(const Stack& parameters, Input input) {
  auto output = std::make_shared<Result>(input.position);
  auto copy = std::make_shared<Stack>(parameters);
  return futures::Transform(
      futures::ForEach(
          copy->stack.begin(), copy->stack.end(),
          [output, input](const transformation::Variant& transformation) {
            Input sub_input(input.buffer);
            sub_input.position = output->position;
            sub_input.mode = input.mode;
            return futures::Transform(
                Apply(transformation, sub_input), [output](Result result) {
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

void Stack::PushBack(Variant transformation) {
  stack.push_back(std::move(transformation));
}

void Stack::PushFront(Variant transformation) {
  stack.push_front(std::move(transformation));
}
}  // namespace transformation

transformation::Variant ComposeTransformation(transformation::Variant a,
                                              transformation::Variant b) {
  return transformation::Stack{{std::move(a), std::move(b)}};
}
}  // namespace afc::editor
