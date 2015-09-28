#include "while_expression.h"

#include <cassert>

#include <glog/logging.h>

#include "compilation.h"
#include "evaluation.h"
#include "../public/vm.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class WhileExpression : public Expression {
 public:
  WhileExpression(unique_ptr<Expression> condition, unique_ptr<Expression> body)
      : condition_(std::move(condition)), body_(std::move(body)) {
    assert(condition_ != nullptr);
    assert(body_ != nullptr);
  }

  const VMType& type() {
    return VMType::Void();
  }

  void Evaluate(OngoingEvaluation* evaluation) {
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [advancer, this](OngoingEvaluation* evaluation) {
          Iteration(advancer, evaluation);
        };
    DVLOG(4) << "Evaluating condition...";
    return condition_->Evaluate(evaluation);
  }
 private:

  void Iteration(std::function<void(OngoingEvaluation* evaluation)> advancer,
                 OngoingEvaluation* evaluation) {
    if (!evaluation->value->boolean) {
      DVLOG(3) << "Iteration is done.";
      evaluation->value = Value::NewVoid();
      evaluation->advancer = advancer;
    }
    DVLOG(5) << "Iterating...";
    evaluation->advancer = [advancer, this](OngoingEvaluation* post_loop) {
      post_loop->advancer = advancer;
      Evaluate(post_loop);
    };
    body_->Evaluate(evaluation);
  }

  unique_ptr<Expression> condition_;
  unique_ptr<Expression> body_;
};

}  // namespace

unique_ptr<Expression> NewWhileExpression(
    Compilation* compilation, unique_ptr<Expression> condition,
    unique_ptr<Expression> body) {
  if (condition == nullptr || body == nullptr) {
    return nullptr;
  }
  if (condition->type().type != VMType::VM_BOOLEAN) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"while\" loop but found \""
        + condition->type().ToString() + L"\".");
    return nullptr;
  }

  return unique_ptr<Expression>(
      new WhileExpression(std::move(condition), std::move(body)));
}

}  // namespace vm
}  // namespace afc
