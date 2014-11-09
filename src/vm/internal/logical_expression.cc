#include "logical_expression.h"

#include <cassert>

#include <glog/logging.h>

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

  void Evaluate(OngoingEvaluation* evaluation) {
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [this, advancer](OngoingEvaluation* inner_evaluation) {
          CHECK_EQ(VMType::VM_BOOLEAN, inner_evaluation->value->type.type);
          inner_evaluation->advancer = advancer;
          if (inner_evaluation->value->boolean == identity_) {
            expr_b_->Evaluate(inner_evaluation);
          }
        };
    expr_a_->Evaluate(evaluation);
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
