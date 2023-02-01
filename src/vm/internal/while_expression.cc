#include "while_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"
#include "append_expression.h"
#include "compilation.h"
#include "src/language/overload.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::VisitCallback;

class WhileExpression : public Expression {
 public:
  WhileExpression(NonNull<std::shared_ptr<Expression>> condition,
                  NonNull<std::shared_ptr<Expression>> body)
      : condition_(std::move(condition)), body_(std::move(body)) {}

  std::vector<VMType> Types() { return {{.variant = types::Void{}}}; }

  std::unordered_set<VMType> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() override {
    return condition_->purity() == PurityType::kPure &&
                   body_->purity() == PurityType::kPure
               ? PurityType::kPure
               : PurityType::kUnknown;
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const VMType&) override {
    DVLOG(4) << "Starting iteration.";
    futures::Future<ValueOrError<EvaluationOutput>> output;
    Iterate(trampoline, condition_, body_, std::move(output.consumer));
    return std::move(output.value);
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<WhileExpression>(condition_, body_);
  }

 private:
  static void Iterate(
      Trampoline& trampoline, NonNull<std::shared_ptr<Expression>> condition,
      NonNull<std::shared_ptr<Expression>> body,
      futures::ValueOrError<EvaluationOutput>::Consumer consumer) {
    trampoline.Bounce(condition.value(), {.variant = types::Bool{}})
        .SetConsumer(VisitCallback(overload{
            [consumer](Error error) { return consumer(std::move(error)); },
            [condition, body, consumer,
             &trampoline](EvaluationOutput condition_output) {
              switch (condition_output.type) {
                case EvaluationOutput::OutputType::kReturn:
                  consumer(std::move(condition_output));
                  return;

                case EvaluationOutput::OutputType::kContinue:
                  if (!condition_output.value.ptr()->get_bool()) {
                    DVLOG(3) << "Iteration is done.";
                    consumer(Success(EvaluationOutput::New(
                        Value::NewVoid(trampoline.pool()))));
                    return;
                  }

                  DVLOG(5) << "Iterating...";
                  trampoline.Bounce(body.value(), body->Types()[0])
                      .SetConsumer(VisitCallback(overload{
                          [consumer](Error error) {
                            consumer(std::move(error));
                          },
                          [condition, body, consumer,
                           &trampoline](EvaluationOutput body_output) {
                            switch (body_output.type) {
                              case EvaluationOutput::OutputType::kReturn:
                                consumer(std::move(body_output));
                                break;
                              case EvaluationOutput::OutputType::kContinue:
                                Iterate(trampoline, std::move(condition),
                                        std::move(body), std::move(consumer));
                            }
                          }}));
              }
            }}));
  }

  const NonNull<std::shared_ptr<Expression>> condition_;
  const NonNull<std::shared_ptr<Expression>> body_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewWhileExpression(
    Compilation* compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> body) {
  if (condition == nullptr || body == nullptr) {
    return Error(L"Input missing.");
  }
  if (!condition->IsBool()) {
    Error error(
        L"Expected bool value for condition of \"while\" loop but found: " +
        TypesToString(condition->Types()) + L".");
    compilation->AddError(error);
    return error;
  }

  return MakeNonNullUnique<WhileExpression>(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(condition)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(body)));
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewForExpression(
    Compilation* compilation, std::unique_ptr<Expression> init,
    std::unique_ptr<Expression> condition, std::unique_ptr<Expression> update,
    std::unique_ptr<Expression> body) {
  if (init == nullptr || condition == nullptr || update == nullptr ||
      body == nullptr) {
    return Error(L"Input missing.");
  }
  ASSIGN_OR_RETURN(
      NonNull<std::unique_ptr<Expression>> body_expression,
      NewAppendExpression(compilation, std::move(body), std::move(update)));
  ASSIGN_OR_RETURN(NonNull<std::unique_ptr<Expression>> while_expression,
                   NewWhileExpression(compilation, std::move(condition),
                                      std::move(body_expression.get_unique())));
  return NewAppendExpression(compilation, std::move(init),
                             std::move(while_expression.get_unique()));
}
}  // namespace afc::vm
