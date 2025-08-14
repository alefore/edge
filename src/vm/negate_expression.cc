#include "src/vm/negate_expression.h"

#include "src/language/error/value_or_error.h"
#include "src/math/numbers.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::math::numbers::Number;

namespace afc::vm {
namespace {

class NegateExpression : public Expression {
  struct ConstructorAccessTag {};

  const std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate_;
  const gc::Ptr<Expression> expr_;

 public:
  static language::gc::Root<NegateExpression> New(
      std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
      gc::Ptr<Expression> expr) {
    gc::Pool& pool = expr.pool();
    return pool.NewRoot(language::MakeNonNullUnique<NegateExpression>(
        ConstructorAccessTag{}, negate, std::move(expr)));
  }

  NegateExpression(
      ConstructorAccessTag,
      std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
      gc::Ptr<Expression> expr)
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

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {expr_.object_metadata()};
  }
};

ValueOrError<gc::Root<Expression>> NewNegateExpression(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> expr_or_error,
    std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
    const Type& expected_type) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> expr, std::move(expr_or_error));
  if (!expr->SupportsType(expected_type))
    return compilation.AddError(
        Error{LazyString{L"Can't negate an expression of type: \""} +
              TypesToString(expr->Types()) + LazyString{L"\""}});
  return NegateExpression::New(negate, std::move(expr));
}
}  // namespace

ValueOrError<gc::Root<Expression>> NewNegateExpressionBool(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewBool(pool, !value.get_bool());
      },
      types::Bool{});
}

ValueOrError<gc::Root<Expression>> NewNegateExpressionNumber(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewNumber(pool,
                                Number::FromInt64(0) - value.get_number());
      },
      types::Number{});
}

}  // namespace afc::vm
