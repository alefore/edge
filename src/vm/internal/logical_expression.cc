#include "logical_expression.h"

#include <glog/logging.h>

#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class LogicalExpression : public Expression {
 public:
  LogicalExpression(bool identity, std::shared_ptr<Expression> expr_a,
                    std::shared_ptr<Expression> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  const VMType& type() { return VMType::Bool(); }

  void Evaluate(Trampoline* trampoline) {
    auto identity = identity_;
    auto expr_a_copy = expr_a_;
    auto expr_b_copy = expr_b_;
    trampoline->Bounce(expr_a_copy.get(),
        [identity, expr_a_copy, expr_b_copy](std::unique_ptr<Value> value,
                                             Trampoline* trampoline) {
          CHECK_EQ(VMType::VM_BOOLEAN, value->type.type);
          if (value->boolean == identity) {
            expr_b_copy->Evaluate(trampoline);
          } else {
            trampoline->Continue(std::move(value));
          }
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<LogicalExpression>(identity_, expr_a_, expr_b_);
  }

 private:
  const bool identity_;
  const std::shared_ptr<Expression> expr_a_;
  const std::shared_ptr<Expression> expr_b_;
};

}

std::unique_ptr<Expression> NewLogicalExpression(
    bool identity, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr
      || a->type().type != VMType::VM_BOOLEAN
      || b->type().type != VMType::VM_BOOLEAN) {
    return nullptr;
  }
  return std::make_unique<LogicalExpression>(
      identity, std::move(a), std::move(b));
}

}  // namespace vm
}  // namespace afc
