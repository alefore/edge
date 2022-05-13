#include "logical_expression.h"

#include <glog/logging.h>

#include "../public/types.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

class LogicalExpression : public Expression {
 public:
  LogicalExpression(bool identity, NonNull<std::shared_ptr<Expression>> expr_a,
                    NonNull<std::shared_ptr<Expression>> expr_b)
      : identity_(identity),
        expr_a_(std::move(expr_a)),
        expr_b_(std::move(expr_b)) {}

  std::vector<VMType> Types() override { return {VMType::Bool()}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() {
    return expr_a_->purity() == PurityType::kPure &&
                   expr_b_->purity() == PurityType::kPure
               ? PurityType::kPure
               : PurityType::kUnknown;
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const VMType& type) override {
    return trampoline.Bounce(*expr_a_, VMType::Bool())
        .Transform([type, &trampoline, identity = identity_,
                    expr_b = expr_b_](EvaluationOutput a_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (a_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(a_output)));
            case EvaluationOutput::OutputType::kContinue:
              return a_output.value.ptr()->get_bool() == identity
                         ? trampoline.Bounce(*expr_b, type)
                         : futures::Past(Success(std::move(a_output)));
          }
          language::Error error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<LogicalExpression>(identity_, expr_a_, expr_b_);
  }

 private:
  const bool identity_;
  const NonNull<std::shared_ptr<Expression>> expr_a_;
  const NonNull<std::shared_ptr<Expression>> expr_b_;
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
  return std::make_unique<LogicalExpression>(
      identity, NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)));
}

}  // namespace afc::vm
