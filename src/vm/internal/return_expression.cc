#include "return_expression.h"

#include <cassert>

#include "compilation.h"
#include "evaluation.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class ReturnExpression : public Expression {
 public:
  ReturnExpression(unique_ptr<Expression> expr)
      : expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  void Evaluate(OngoingEvaluation* evaluation) {
    evaluation->advancer = evaluation->return_advancer;
    expr_->Evaluate(evaluation);
  }

 private:
  unique_ptr<Expression> expr_;
};

}  // namespace

unique_ptr<Expression> NewReturnExpression(
    Compilation* compilation, unique_ptr<Expression> expr) {
  if (expr == nullptr) {
    return nullptr;
  }

  assert(!compilation->return_types.empty());
  const VMType& expected_type = compilation->return_types.back();
  if (!(expected_type == expr->type())) {
    compilation->errors.push_back(
        L"Returning value of type \"" + expr->type().ToString()
        + L"\" but expected \"" + expected_type.ToString() + L"\"");
    return nullptr;
  }
  return unique_ptr<Expression>(new ReturnExpression(std::move(expr)));
}

}  // namespace vm
}  // namespace afc
