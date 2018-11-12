#include "return_expression.h"

#include <glog/logging.h>

#include "compilation.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class ReturnExpression : public Expression {
 public:
  ReturnExpression(std::shared_ptr<Expression> expr) : expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  void Evaluate(Trampoline* trampoline) override {
    auto expr = expr_;
    trampoline->Bounce(expr.get(),
        // We do this silly dance just to capture expr.
        [expr](Value::Ptr value, Trampoline* trampoline) {
          trampoline->Return(std::move(value));
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<ReturnExpression>(expr_);
  }

 private:
  const std::shared_ptr<Expression> expr_;
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
