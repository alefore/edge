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
    CHECK(cond_->IsBool());
    CHECK(true_case_ != nullptr);
    CHECK(false_case_ != nullptr);
  }

  std::vector<VMType> Types() { return true_case_->Types(); }

  void Evaluate(Trampoline* trampoline, const VMType& type) {
    auto cond_copy = cond_;
    auto true_copy = true_case_;
    auto false_copy = false_case_;
    trampoline->Bounce(
        cond_.get(), VMType::Bool(),
        [type, cond_copy, true_copy, false_copy](std::unique_ptr<Value> result,
                                                 Trampoline* trampoline) {
          (result->boolean ? true_copy : false_copy)
              ->Evaluate(trampoline, type);
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

  if (!condition->IsBool()) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"if\" expression but found " +
        TypesToString(condition->Types()) + L".");
    return nullptr;
  }

  if (!(true_case->Types() == false_case->Types())) {
    compilation->errors.push_back(
        L"Type mismatch between branches of conditional expression: " +
        TypesToString(true_case->Types()) + L" and " +
        TypesToString(false_case->Types()) + L".");
    return nullptr;
  }

  return std::make_unique<IfExpression>(
      std::move(condition), std::move(true_case), std::move(false_case));
}

}  // namespace vm
}  // namespace afc
