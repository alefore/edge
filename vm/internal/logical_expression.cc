#include "logical_expression.h"

#include <cassert>

#include "evaluation.h"
#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class LogicalExpression : public Expression {
 public:
  LogicalExpression(
      bool identity,
      unique_ptr<Expression> expr_a,
      unique_ptr<Expression> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  const VMType& type() { return VMType::Bool(); }

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    return expr_a_->Evaluate(Evaluation(evaluation, Continuation(
        [this, evaluation](unique_ptr<Value> value_a) {
          assert(value_a->type.type == VMType::VM_BOOLEAN);
          if (value_a->boolean != identity_) {
            return make_pair(evaluation.continuation, std::move(value_a));
          }
          return expr_b_->Evaluate(evaluation);
        })));
  }

 private:
  bool identity_;
  unique_ptr<Expression> expr_a_;
  unique_ptr<Expression> expr_b_;
};

}

unique_ptr<Expression> NewLogicalExpression(
    bool identity, unique_ptr<Expression> a, unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr
      || a->type().type != VMType::VM_BOOLEAN
      || b->type().type != VMType::VM_BOOLEAN) {
    return nullptr;
  }
  return unique_ptr<Expression>(
      new LogicalExpression(identity, std::move(a), std::move(b)));
}

}  // namespace vm
}  // namespace afc
