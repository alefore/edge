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
    CHECK(condition_ != nullptr);
    CHECK(body_ != nullptr);
  }

  const VMType& type() {
    return VMType::Void();
  }

  void Evaluate(Trampoline* trampoline) {
    DVLOG(4) << "Evaluating condition...";
    trampoline->Bounce(condition_.get(),
        [this](std::unique_ptr<Value> value, Trampoline* trampoline) {
          Iteration(trampoline, value->boolean);
        });
  }
 private:

  void Iteration(Trampoline* trampoline, bool cond_value) {
    if (!cond_value) {
      DVLOG(3) << "Iteration is done.";
      trampoline->Continue(Value::NewVoid());
      return;
    }

    DVLOG(5) << "Iterating...";
    trampoline->Bounce(body_.get(),
        [this](std::unique_ptr<Value>, Trampoline* trampoline) {
          Evaluate(trampoline);
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
        L"Expected bool value for condition of \"while\" loop but found \""
        + condition->type().ToString() + L"\".");
    return nullptr;
  }

  return unique_ptr<Expression>(
      new WhileExpression(std::move(condition), std::move(body)));
}

}  // namespace vm
}  // namespace afc
