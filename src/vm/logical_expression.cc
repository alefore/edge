#include "logical_expression.h"

#include <glog/logging.h>

#include "src/language/error/value_or_error.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NewError;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

class LogicalExpression : public Expression {
 public:
  LogicalExpression(bool identity, NonNull<std::shared_ptr<Expression>> expr_a,
                    NonNull<std::shared_ptr<Expression>> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  std::vector<Type> Types() override { return {types::Bool{}}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    return expr_a_->purity() == PurityType::kPure &&
                   expr_b_->purity() == PurityType::kPure
               ? PurityType::kPure
               : PurityType::kUnknown;
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    return trampoline.Bounce(expr_a_, types::Bool{})
        .Transform([type, &trampoline, identity = identity_,
                    expr_b = expr_b_](EvaluationOutput a_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (a_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(a_output)));
            case EvaluationOutput::OutputType::kContinue:
              return a_output.value.ptr()->get_bool() == identity
                         ? trampoline.Bounce(expr_b, type)
                         : futures::Past(Success(std::move(a_output)));
          }
          language::Error error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

 private:
  const bool identity_;
  const NonNull<std::shared_ptr<Expression>> expr_a_;
  const NonNull<std::shared_ptr<Expression>> expr_b_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewLogicalExpression(
    Compilation& compilation, bool identity, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return Error(L"Missing inputs");
  }
  if (!a->IsBool()) {
    Error error = NewError(LazyString{L"Expected `bool` value but found: "} +
                           TypesToString(a->Types()));
    compilation.AddError(error);
    return error;
  }
  if (!b->IsBool()) {
    Error error = NewError(LazyString{L"Expected `bool` value but found: "} +
                           TypesToString(b->Types()));
    compilation.AddError(error);
    return error;
  }
  return MakeNonNullUnique<LogicalExpression>(
      identity, NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)));
}

}  // namespace afc::vm
