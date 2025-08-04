#include "return_expression.h"

#include <glog/logging.h>

#include "src/language/gc.h"
#include "src/vm/delegating_expression.h"

namespace gc = afc::language::gc;

using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::VisitOptional;

namespace afc::vm {
namespace {
class ReturnExpression : public Expression {
  struct ConstructorAccessTag {};

  const gc::Ptr<Expression> expr_;

 public:
  static gc::Root<ReturnExpression> New(gc::Ptr<Expression> expr) {
    gc::Pool& pool = expr.pool();
    return pool.NewRoot(
        MakeNonNullUnique<ReturnExpression>(ConstructorAccessTag{}, expr));
  }

  ReturnExpression(ConstructorAccessTag, gc::Ptr<Expression> expr)
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

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {expr_.object_metadata()};
  }
};

}  // namespace

// TODO(2025-08-01, trivial): receive expr_input as gc::Ptr.
std::optional<gc::Root<Expression>> NewReturnExpression(
    std::optional<gc::Root<Expression>> expr_input) {
  return VisitOptional(
      [](gc::Root<Expression> expr) -> std::optional<gc::Root<Expression>> {
        return ReturnExpression::New(expr.ptr());
      },
      [] { return std::optional<gc::Root<Expression>>{}; },
      std::move(expr_input));
}

}  // namespace afc::vm
