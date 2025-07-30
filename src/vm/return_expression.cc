#include "return_expression.h"

#include <glog/logging.h>

using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::VisitPointer;

namespace afc::vm {
namespace {
class ReturnExpression : public Expression {
 public:
  ReturnExpression(NonNull<std::shared_ptr<Expression>> expr)
      : expr_(std::move(expr)) {}

  std::vector<Type> Types() override { return expr_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    auto types = expr_->Types();
    return {types.cbegin(), types.cend()};
  }

  PurityType purity() override { return expr_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type&) override {
    return trampoline.Bounce(expr_, expr_->Types()[0])
        .Transform([](EvaluationOutput expr_output) {
          return Success(
              EvaluationOutput::Return(std::move(expr_output.value)));
        });
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand() const override {
    return {};
  }

 private:
  const NonNull<std::shared_ptr<Expression>> expr_;
};

}  // namespace

std::unique_ptr<Expression> NewReturnExpression(
    std::unique_ptr<Expression> expr_input) {
  return VisitPointer(
      std::move(expr_input),
      [](NonNull<std::unique_ptr<Expression>> expr) {
        return std::make_unique<ReturnExpression>(std::move(expr));
      },
      [] { return nullptr; });
}

}  // namespace afc::vm
