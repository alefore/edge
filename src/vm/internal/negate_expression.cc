#include "negate_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

class NegateExpression : public Expression {
 public:
  NegateExpression(std::function<void(Value*)> negate,
                   NonNull<std::shared_ptr<Expression>> expr)
      : negate_(negate), expr_(std::move(expr)) {}

  std::vector<VMType> Types() override { return expr_->Types(); }
  std::unordered_set<VMType> ReturnTypes() const override {
    return expr_->ReturnTypes();
  }

  PurityType purity() override { return expr_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                                   const VMType&) override {
    return trampoline->Bounce(*expr_, expr_->Types()[0])
        .Transform([negate = negate_](EvaluationOutput expr_output) {
          negate(expr_output.value.get());
          return Success(EvaluationOutput::New(std::move(expr_output.value)));
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<NegateExpression>(negate_, std::move(expr_));
  }

 private:
  const std::function<void(Value*)> negate_;
  const NonNull<std::shared_ptr<Expression>> expr_;
};

}  // namespace

std::unique_ptr<Expression> NewNegateExpression(
    std::function<void(Value*)> negate, const VMType& expected_type,
    Compilation* compilation, std::unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (!expr->SupportsType(expected_type)) {
    compilation->errors.push_back(L"Can't negate an expression of type: \"" +
                                  TypesToString(expr->Types()) + L"\"");
    return nullptr;
  }
  return std::make_unique<NegateExpression>(
      negate, NonNull<std::shared_ptr<Expression>>::Unsafe(std::move(expr)));
}

}  // namespace afc::vm
