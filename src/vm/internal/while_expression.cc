#include "while_expression.h"

#include <cassert>

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

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    return condition_
        ->Evaluate(Evaluation(evaluation, iterator(evaluation)));
  }

 private:
  Continuation iterator(const Evaluation& evaluation) {
    return Continuation(
        [this, evaluation](unique_ptr<Value> condition_value) {
          if (!condition_value->boolean) {
            return make_pair(evaluation.continuation, Value::NewVoid());
          }
          return body_->Evaluate(Evaluation(evaluation, Continuation(
              [this, evaluation](unique_ptr<Value> ignored_value) {
                return Evaluate(evaluation);
              })));
        });
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
        "Expected bool value for condition of \"while\" loop but found \""
        + condition->type().ToString() + "\".");
    return nullptr;
  }

  return unique_ptr<Expression>(
      new WhileExpression(std::move(condition), std::move(body)));
}

}  // namespace vm
}  // namespace afc
