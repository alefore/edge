#include "src/transformation/stack.h"

#include "src/buffer.h"
#include "src/log.h"
#include "src/run_command_handler.h"
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
  return futures::ForEach(
             copy->stack.begin(), copy->stack.end(),
             [output, input,
              trace](const transformation::Variant& transformation) {
               trace->Append(L"Transformation: " + ToString(transformation));
               return Apply(transformation, input.NewChild(output->position))
                   .Transform([output](Result result) {
                     output->MergeFrom(std::move(result));
                     return output->success
                                ? futures::IterationControlCommand::kContinue
                                : futures::IterationControlCommand::kStop;
                   });
             })
      .Transform([output, input, copy](futures::IterationControlCommand) {
        Delete delete_transformation{
            .modifiers = {.direction = input.position < output->position
                                           ? Direction::kForwards
                                           : Direction::kBackwards},
            .range = Range(min(input.position, output->position),
                           max(input.position, output->position))};
        switch (copy->post_transformation_behavior) {
          case Stack::PostTransformationBehavior::kNone:
            return futures::Past(std::move(*output));
          case Stack::PostTransformationBehavior::kDeleteRegion:
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCopyRegion:
            delete_transformation.modifiers.delete_behavior =
                Modifiers::DeleteBehavior::kDoNothing;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCommandSystem:
            if (input.mode == Input::Mode::kPreview) {
              delete_transformation.preview_modifiers = {
                  LineModifier::GREEN, LineModifier::UNDERLINE};
              return Apply(delete_transformation,
                           input.NewChild(delete_transformation.range->begin));
            }
            auto contents = input.buffer->contents()->copy();
            contents->FilterToRange(*delete_transformation.range);
            ForkCommand(input.buffer->editor(),
                        ForkCommandOptions{.command = contents->ToString()});
            return futures::Past(std::move(*output));
        }
        LOG(FATAL) << "Invalid post transformation behavior.";
        return futures::Past(std::move(*output));
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
