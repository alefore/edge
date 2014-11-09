#include "negate_expression.h"

#include "compilation.h"
#include "evaluation.h"
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

  void Evaluate(OngoingEvaluation* evaluation) {
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [this, advancer](OngoingEvaluation* inner_evaluation) {
          negate_(inner_evaluation->value.get());
          inner_evaluation->advancer = advancer;
        };
    expr_->Evaluate(evaluation);
  }

 private:
  std::function<void(Value*)> negate_;
  unique_ptr<Expression> expr_;
};

}  // namespace

unique_ptr<Expression> NewNegateExpression(
    std::function<void(Value*)> negate,
    const VMType& expected_type,
    Compilation* compilation,
    unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (!(expr->type() == expected_type)) {
    compilation->errors.push_back("Can't negate an expression of type: "
                                  + expr->type().ToString());
    return nullptr;
  }
  return unique_ptr<Expression>(
      new NegateExpression(negate, std::move(expr)));
}

}  // namespace vm
}  // namespace afc
