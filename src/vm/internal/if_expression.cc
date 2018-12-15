#include "if_expression.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class IfExpression : public Expression {
 public:
  IfExpression(std::shared_ptr<Expression> cond,
               std::shared_ptr<Expression> true_case,
               std::shared_ptr<Expression> false_case)
      : cond_(std::move(cond)),
        true_case_(std::move(true_case)),
        false_case_(std::move(false_case)) {
    CHECK(cond_ != nullptr);
    CHECK(true_case_ != nullptr);
    CHECK(false_case_ != nullptr);
  }

  const VMType& type() { return true_case_->type(); }

  void Evaluate(Trampoline* trampoline) {
    auto cond_copy = cond_;
    auto true_copy = true_case_;
    auto false_copy = false_case_;
    trampoline->Bounce(cond_.get(), [cond_copy, true_copy, false_copy](
                                        std::unique_ptr<Value> result,
                                        Trampoline* trampoline) {
      (result->boolean ? true_copy : false_copy)->Evaluate(trampoline);
    });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<IfExpression>(cond_, true_case_, false_case_);
  }

 private:
  const std::shared_ptr<Expression> cond_;
  const std::shared_ptr<Expression> true_case_;
  const std::shared_ptr<Expression> false_case_;
};

}  // namespace

std::unique_ptr<Expression> NewIfExpression(
    Compilation* compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> true_case,
    std::unique_ptr<Expression> false_case) {
  if (condition == nullptr || true_case == nullptr || false_case == nullptr) {
    return nullptr;
  }

  if (condition->type().type != VMType::VM_BOOLEAN) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"if\" expression but found \"" +
        condition->type().ToString() + L"\".");
    return nullptr;
  }

  if (!(true_case->type() == false_case->type())) {
    compilation->errors.push_back(
        L"Type mismatch between branches of conditional expression: \"" +
        true_case->type().ToString() + L"\" and \"" +
        false_case->type().ToString() + L"\"");
    return nullptr;
  }

  return std::make_unique<IfExpression>(
      std::move(condition), std::move(true_case), std::move(false_case));
}

}  // namespace vm
}  // namespace afc
