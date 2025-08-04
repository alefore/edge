#include "logical_expression.h"

#include <glog/logging.h>

#include "src/language/error/value_or_error.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

class LogicalExpression : public Expression {
  struct ConstructorAccessTag {};

  const bool identity_;
  const gc::Ptr<Expression> expr_a_;
  const gc::Ptr<Expression> expr_b_;

 public:
  static language::gc::Root<LogicalExpression> New(bool identity,
                                                   gc::Ptr<Expression> expr_a,
                                                   gc::Ptr<Expression> expr_b) {
    gc::Pool& pool = expr_a.pool();
    return pool.NewRoot(language::MakeNonNullUnique<LogicalExpression>(
        ConstructorAccessTag{}, identity, std::move(expr_a),
        std::move(expr_b)));
  }

  LogicalExpression(ConstructorAccessTag, bool identity,
                    gc::Ptr<Expression> expr_a, gc::Ptr<Expression> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  std::vector<Type> Types() override { return {types::Bool{}}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    return CombinePurityType({expr_a_->purity(), expr_b_->purity()});
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    return trampoline.Bounce(expr_a_, types::Bool{})
        .Transform([type, &trampoline, identity = identity_,
                    expr_b_root = expr_b_.ToRoot()](EvaluationOutput a_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (a_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(a_output)));
            case EvaluationOutput::OutputType::kContinue:
              return a_output.value.ptr()->get_bool() == identity
                         ? trampoline.Bounce(expr_b_root.ptr(), type)
                         : futures::Past(Success(std::move(a_output)));
          }
          language::Error error{LazyString{L"Unhandled OutputType case."}};
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {expr_a_.object_metadata(), expr_b_.object_metadata()};
  }
};

}  // namespace

ValueOrError<gc::Root<Expression>> NewLogicalExpression(
    Compilation& compilation, bool identity,
    std::optional<gc::Root<Expression>> a,
    std::optional<gc::Root<Expression>> b) {
  if (a == std::nullopt || b == std::nullopt)
    return Error{LazyString{L"Missing inputs"}};

  if (!a.value()->IsBool())
    return compilation.AddError(
        Error{LazyString{L"Expected `bool` value but found: "} +
              TypesToString(a.value()->Types())});

  if (!b.value()->IsBool())
    return compilation.AddError(
        Error{LazyString{L"Expected `bool` value but found: "} +
              TypesToString(b.value()->Types())});

  return LogicalExpression::New(identity, std::move(a)->ptr(),
                                std::move(b)->ptr());
}

}  // namespace afc::vm
