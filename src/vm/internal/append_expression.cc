#include "append_expression.h"

#include "evaluation.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class AppendExpression : public Expression {
 public:
  AppendExpression(unique_ptr<Expression> e0, unique_ptr<Expression> e1)
      : e0_(std::move(e0)), e1_(std::move(e1)) {}

  const VMType& type() { return e1_->type(); }

  void Evaluate(OngoingEvaluation* evaluation) {
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [this, advancer](OngoingEvaluation* inner_evaluation) {
          inner_evaluation->advancer = advancer;
          e1_->Evaluate(inner_evaluation);
        };
    e0_->Evaluate(evaluation);
  }

 private:
  unique_ptr<Expression> e0_;
  unique_ptr<Expression> e1_;
};

}  // namespace

unique_ptr<Expression> NewAppendExpression(
    unique_ptr<Expression> a, unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  return unique_ptr<Expression>(
      new AppendExpression(std::move(a), std::move(b)));
}

}  // namespace
}  // namespace afc
