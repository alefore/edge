#include "negate_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc::vm {
namespace {
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

  std::vector<VMType> Types() override { return expr_->Types(); }
  std::unordered_set<VMType> ReturnTypes() const override {
    return expr_->ReturnTypes();
  }

  PurityType purity() override { return expr_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const VMType&) override {
    return trampoline.Bounce(*expr_, expr_->Types()[0])
        .Transform([&pool = trampoline.pool(),
                    negate = negate_](EvaluationOutput expr_output) {
          return Success(EvaluationOutput::New(
              negate(pool, expr_output.value.ptr().value())));
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<NegateExpression>(negate_, std::move(expr_));
  }

 private:
  const std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate_;
  const NonNull<std::shared_ptr<Expression>> expr_;
};

std::unique_ptr<Expression> NewNegateExpression(
    Compilation& compilation, std::unique_ptr<Expression> expr,
    std::function<gc::Root<Value>(gc::Pool& pool, Value&)> negate,
    const VMType& expected_type) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (!expr->SupportsType(expected_type)) {
    compilation.errors.push_back(L"Can't negate an expression of type: \"" +
                                 TypesToString(expr->Types()) + L"\"");
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
      VMType::Bool());
}

std::unique_ptr<Expression> NewNegateExpressionInt(
    Compilation& compilation, std::unique_ptr<Expression> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewInt(pool, -value.get_int());
      },
      VMType::Int());
}

std::unique_ptr<Expression> NewNegateExpressionDouble(
    Compilation& compilation, std::unique_ptr<Expression> expr) {
  return NewNegateExpression(
      compilation, std::move(expr),
      [](gc::Pool& pool, Value& value) {
        return Value::NewDouble(pool, -value.get_double());
      },
      VMType::Double());
}

}  // namespace afc::vm
