#include "logical_expression.h"

#include <glog/logging.h>

#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"

namespace afc {
namespace vm {

namespace {

class LogicalExpression : public Expression {
 public:
  LogicalExpression(bool identity, std::shared_ptr<Expression> expr_a,
                    std::shared_ptr<Expression> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  std::vector<VMType> Types() override { return {VMType::Bool()}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  futures::DelayedValue<EvaluationOutput> Evaluate(
      Trampoline* trampoline, const VMType& type) override {
    return futures::Transform(
        trampoline->Bounce(expr_a_.get(), VMType::Bool()),
        [type, trampoline, identity = identity_,
         expr_b = expr_b_](EvaluationOutput a_output) {
          return a_output.value->boolean == identity
                     ? trampoline->Bounce(expr_b.get(), type)
                     : futures::ImmediateValue(std::move(a_output));
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<LogicalExpression>(identity_, expr_a_, expr_b_);
  }

 private:
  const bool identity_;
  const std::shared_ptr<Expression> expr_a_;
  const std::shared_ptr<Expression> expr_b_;
};

}  // namespace

std::unique_ptr<Expression> NewLogicalExpression(
    Compilation* compilation, bool identity, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  if (!a->IsBool()) {
    compilation->errors.push_back(L"Expected `bool` value but found: " +
                                  TypesToString(a->Types()));
    return nullptr;
  }
  if (!b->IsBool()) {
    compilation->errors.push_back(L"Expected `bool` value but found: " +
                                  TypesToString(b->Types()));
    return nullptr;
  }
  return std::make_unique<LogicalExpression>(identity, std::move(a),
                                             std::move(b));
}

}  // namespace vm
}  // namespace afc
