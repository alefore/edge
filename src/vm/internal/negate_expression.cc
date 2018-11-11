#include "negate_expression.h"

#include "compilation.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class NegateExpression : public Expression {
 public:
  NegateExpression(std::function<void(Value*)> negate,
                   unique_ptr<Expression> expr)
      : negate_(negate),
        expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  void Evaluate(Trampoline* trampoline) {
    trampoline->Bounce(expr_.get(),
        [this](std::unique_ptr<Value> value, Trampoline* trampoline) {
          negate_(value.get());
          trampoline->Continue(std::move(value));
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<NegateExpression>(negate_, expr_->Clone());
  }

 private:
  std::function<void(Value*)> negate_;
  unique_ptr<Expression> expr_;
};

}  // namespace

std::unique_ptr<Expression> NewNegateExpression(
    std::function<void(Value*)> negate,
    const VMType& expected_type,
    Compilation* compilation,
    std::unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (!(expr->type() == expected_type)) {
    compilation->errors.push_back(
        L"Can't negate an expression of type: \"" + expr->type().ToString()
        + L"\"");
    return nullptr;
  }
  return std::make_unique<NegateExpression>(negate, std::move(expr));
}

}  // namespace vm
}  // namespace afc
