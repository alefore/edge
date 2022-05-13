#include "return_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

class ReturnExpression : public Expression {
 public:
  ReturnExpression(NonNull<std::shared_ptr<Expression>> expr)
      : expr_(std::move(expr)) {}

  std::vector<VMType> Types() override { return expr_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    auto types = expr_->Types();
    return {types.cbegin(), types.cend()};
  }

  PurityType purity() override { return expr_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const VMType&) override {
    return trampoline.Bounce(expr_.value(), expr_->Types()[0])
        .Transform([](EvaluationOutput expr_output) {
          return Success(
              EvaluationOutput::Return(std::move(expr_output.value)));
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<ReturnExpression>(expr_);
  }

 private:
  const NonNull<std::shared_ptr<Expression>> expr_;
};

}  // namespace

std::unique_ptr<Expression> NewReturnExpression(
    Compilation*, std::unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }

  return std::make_unique<ReturnExpression>(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(expr)));
}

}  // namespace afc::vm
