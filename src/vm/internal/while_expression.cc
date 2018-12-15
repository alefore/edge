#include "while_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

class WhileExpression : public Expression {
 public:
  WhileExpression(std::shared_ptr<Expression> condition,
                  std::shared_ptr<Expression> body)
      : condition_(std::move(condition)), body_(std::move(body)) {
    CHECK(condition_ != nullptr);
    CHECK(body_ != nullptr);
  }

  const VMType& type() { return VMType::Void(); }

  void Evaluate(Trampoline* trampoline) override {
    DVLOG(4) << "Evaluating condition...";
    Iterate(trampoline, condition_, body_);
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<WhileExpression>(condition_, body_);
  }

 private:
  static void Iterate(Trampoline* trampoline,
                      std::shared_ptr<Expression> condition,
                      std::shared_ptr<Expression> body) {
    trampoline->Bounce(condition.get(), [condition, body](
                                            std::unique_ptr<Value> cond_value,
                                            Trampoline* trampoline) {
      if (!cond_value->boolean) {
        DVLOG(3) << "Iteration is done.";
        trampoline->Continue(Value::NewVoid());
        return;
      }

      DVLOG(5) << "Iterating...";
      trampoline->Bounce(body.get(), [condition, body](std::unique_ptr<Value>,
                                                       Trampoline* trampoline) {
        Iterate(trampoline, condition, body);
      });
    });
  }

  const std::shared_ptr<Expression> condition_;
  const std::shared_ptr<Expression> body_;
};

}  // namespace

std::unique_ptr<Expression> NewWhileExpression(
    Compilation* compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> body) {
  if (condition == nullptr || body == nullptr) {
    return nullptr;
  }
  if (condition->type().type != VMType::VM_BOOLEAN) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"while\" loop but found \"" +
        condition->type().ToString() + L"\".");
    return nullptr;
  }

  return std::make_unique<WhileExpression>(std::move(condition),
                                           std::move(body));
}

}  // namespace vm
}  // namespace afc
