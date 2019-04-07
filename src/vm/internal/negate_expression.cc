#include "negate_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

class NegateExpression : public Expression {
 public:
  NegateExpression(std::function<void(Value*)> negate,
                   unique_ptr<Expression> expr)
      : negate_(negate), expr_(std::move(expr)) {}

  std::vector<VMType> Types() override { return expr_->Types(); }

  void Evaluate(Trampoline* trampoline, const VMType&) override {
    auto negate = negate_;
    auto expr = expr_;
    trampoline->Bounce(
        expr.get(), expr->Types()[0],
        [negate, expr](std::unique_ptr<Value> value, Trampoline* trampoline) {
          negate(value.get());
          trampoline->Continue(std::move(value));
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<NegateExpression>(negate_, expr_->Clone());
  }

 private:
  const std::function<void(Value*)> negate_;
  const std::shared_ptr<Expression> expr_;
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
  return std::make_unique<NegateExpression>(negate, std::move(expr));
}

}  // namespace vm
}  // namespace afc
