#include "src/vm/negate_expression.h"

#include "src/language/error/value_or_error.h"
#include "src/math/numbers.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

using afc::language::MakeNonNullShared;
using afc::math::numbers::Number;

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

namespace gc = language::gc;

class NegateExpression : public Expression {
 public:
  NegateExpression(
      std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
      NonNull<std::shared_ptr<Expression>> expr)
      : negate_(negate), expr_(std::move(expr)) {}

  std::vector<Type> Types() override { return expr_->Types(); }
  std::unordered_set<Type> ReturnTypes() const override {
    return expr_->ReturnTypes();
  }

  PurityType purity() override { return expr_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type&) override {
    return trampoline.Bounce(expr_, expr_->Types()[0])
        .Transform([&pool = trampoline.pool(),
                    negate = negate_](EvaluationOutput expr_output) {
          return Success(EvaluationOutput::New(
              negate(pool, expr_output.value.ptr().value())));
        });
  }

 private:
  const std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate_;
  const NonNull<std::shared_ptr<Expression>> expr_;
};

std::unique_ptr<Expression> NewNegateExpression(
    Compilation& compilation, std::unique_ptr<Expression> expr,
    std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
    const Type& expected_type) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (!expr->SupportsType(expected_type)) {
    compilation.AddError(Error(L"Can't negate an expression of type: \"" +
                               TypesToString(expr->Types()) + L"\""));
    return nullptr;
  }
  return std::make_unique<NegateExpression>(
      negate, NonNull<std::shared_ptr<Expression>>::Unsafe(std::move(expr)));
}
}  // namespace

std::unique_ptr<Expression> NewNegateExpressionBool(
    Compilation& compilation, std::unique_ptr<Expression> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewBool(pool, !value.get_bool());
      },
      types::Bool{});
}

std::unique_ptr<Expression> NewNegateExpressionNumber(
    Compilation& compilation, std::unique_ptr<Expression> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewNumber(pool, -value.get_number());
      },
      types::Number{});
}

}  // namespace afc::vm
