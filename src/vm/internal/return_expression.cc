#include "return_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

class ReturnExpression : public Expression {
 public:
  ReturnExpression(std::shared_ptr<Expression> expr) : expr_(std::move(expr)) {}

  std::vector<VMType> Types() { return expr_->Types(); }

  void Evaluate(Trampoline* trampoline, const VMType&) override {
    auto expr = expr_;
    trampoline->Bounce(expr.get(), expr->Types()[0],
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
  if (!expr->SupportsType(expected_type)) {
    compilation->errors.push_back(L"Expected return type of " +
                                  expected_type.ToString() + L" but found " +
                                  TypesToString(expr->Types()));
    return nullptr;
  }
  return std::make_unique<ReturnExpression>(std::move(expr));
}

}  // namespace vm
}  // namespace afc
