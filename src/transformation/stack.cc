#include "src/transformation/stack.h"

#include "src/buffer.h"
#include "src/log.h"
#include "src/run_command_handler.h"
#include "src/transformation/composite.h"
#include "src/transformation/input.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace transformation {
namespace {
void ShowValue(OpenBuffer& buffer, const Value& value) {
  if (value.IsVoid()) return;
  std::ostringstream oss;
  oss << "Evaluation result: " << value;
  buffer.status()->SetInformationText(FromByteString(oss.str()));
}

futures::Value<PossibleError> PreviewCppExpression(
    OpenBuffer* buffer, const BufferContents& expression_str) {
  std::wstring errors;
  std::shared_ptr<Expression> expression =
      buffer->CompileString(expression_str.ToString(), &errors);
  if (expression == nullptr) {
    return futures::Past(PossibleError(Error(errors)));
  }

  buffer->status()->Reset();
  switch (expression->purity()) {
    case vm::Expression::PurityType::kPure:
      return buffer->EvaluateExpression(expression.get())
          .Transform([buffer, expression](std::unique_ptr<Value> value) {
            ShowValue(*buffer, *value);
            return Success();
          });
    case vm::Expression::PurityType::kUnknown:
      break;
  }
  return futures::Past(Success());
}

futures::Value<Result> HandleCommandCpp(Input input,
                                        Delete original_delete_transformation) {
  auto contents = input.buffer->contents()->copy();
  contents->FilterToRange(*original_delete_transformation.range);
  if (input.mode == Input::Mode::kPreview) {
    auto delete_transformation =
        std::make_shared<Delete>(std::move(original_delete_transformation));
    delete_transformation->preview_modifiers = {LineModifier::GREEN,
                                                LineModifier::UNDERLINE};
    return PreviewCppExpression(input.buffer, *contents)
        .ConsumeErrors(
            [buffer = input.buffer, delete_transformation](Error error) {
              delete_transformation->preview_modifiers = {
                  LineModifier::RED, LineModifier::UNDERLINE};
              buffer->status()->SetInformationText(error.description);
              return EmptyValue();
            })
        .Transform([delete_transformation, input](EmptyValue) {
          return Apply(*delete_transformation,
                       input.NewChild(delete_transformation->range->begin));
        });
  }
  auto expression = input.buffer->EvaluateString(contents->ToString());
  if (expression == std::nullopt) {
    return futures::Past(Result(input.position));
  }
  return expression->Transform([input](std::unique_ptr<Value> value) {
    CHECK(value != nullptr);
    ShowValue(*input.buffer, *value);
    return Result(input.position);
  });
}
}  // namespace

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
        if (delete_transformation.range->IsEmpty()) {
          return futures::Past(std::move(*output));
        }
        switch (copy->post_transformation_behavior) {
          case Stack::PostTransformationBehavior::kNone: {
            auto contents = input.buffer->contents()->copy();
            contents->FilterToRange(*delete_transformation.range);
            return PreviewCppExpression(input.buffer, *contents)
                .ConsumeErrors([](Error) { return EmptyValue(); })
                .Transform([output](EmptyValue) {
                  return futures::Past(std::move(*output));
                });
          }
          case Stack::PostTransformationBehavior::kDeleteRegion:
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCopyRegion:
            delete_transformation.modifiers.delete_behavior =
                Modifiers::DeleteBehavior::kDoNothing;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCommandSystem: {
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
          case Stack::PostTransformationBehavior::kCommandCpp:
            return HandleCommandCpp(std::move(input), delete_transformation);
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
