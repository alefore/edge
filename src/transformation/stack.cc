#include "src/transformation/stack.h"

#include "src/buffer.h"
#include "src/log.h"
#include "src/transformation/composite.h"
#include "src/transformation/input.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace transformation {
futures::Value<Result> ApplyBase(const Stack& parameters, Input input) {
  auto output = std::make_shared<Result>(input.position);
  auto copy = std::make_shared<Stack>(parameters);
  std::shared_ptr<Log> trace =
      input.buffer->log()->NewChild(L"ApplyBase(Stack)");
  return futures::Transform(
      futures::ForEach(
          copy->stack.begin(), copy->stack.end(),
          [output, input,
           trace](const transformation::Variant& transformation) {
            trace->Append(L"Transformation: " + ToString(transformation));
            return futures::Transform(
                Apply(transformation, input.NewChild(output->position)),
                [output](Result result) {
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

std::wstring ToStringBase(const Stack& stack) {
  std::wstring output = L"Stack(";
  std::wstring separator;
  for (auto& v : stack.stack) {
    output += separator + ToString(v);
    separator = L", ";
  }
  output += L")";
  return output;
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
