#include "return_expression.h"

#include <glog/logging.h>

#include "compilation.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class ReturnExpression : public Expression {
 public:
  ReturnExpression(unique_ptr<Expression> expr)
      : expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  void Evaluate(Trampoline* trampoline) override {
    trampoline->Bounce(expr_.get(), trampoline->return_continuation());
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<ReturnExpression>(expr_->Clone());
  }

 private:
  const std::unique_ptr<Expression> expr_;
};

}  // namespace

std::unique_ptr<Expression> NewReturnExpression(
    Compilation* compilation, std::unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }

  CHECK(!compilation->return_types.empty());
  const VMType& expected_type = compilation->return_types.back();
  if (!(expected_type == expr->type())) {
    compilation->errors.push_back(
        L"Returning value of type \"" + expr->type().ToString()
        + L"\" but expected \"" + expected_type.ToString() + L"\"");
    return nullptr;
  }
  return std::make_unique<ReturnExpression>(std::move(expr));
}

}  // namespace vm
}  // namespace afc
