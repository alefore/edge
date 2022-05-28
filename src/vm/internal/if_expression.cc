#include "if_expression.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/value.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

class IfExpression : public Expression {
 public:
  IfExpression(NonNull<std::shared_ptr<Expression>> cond,
               NonNull<std::shared_ptr<Expression>> true_case,
               NonNull<std::shared_ptr<Expression>> false_case,
               std::unordered_set<VMType> return_types)
      : cond_(std::move(cond)),
        true_case_(std::move(true_case)),
        false_case_(std::move(false_case)),
        return_types_(std::move(return_types)) {
    CHECK(cond_->IsBool());
  }

  std::vector<VMType> Types() override { return true_case_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override {
    return cond_->purity() == PurityType::kPure &&
                   true_case_->purity() == PurityType::kPure &&
                   false_case_->purity() == PurityType::kPure
               ? PurityType::kPure
               : PurityType::kUnknown;
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const VMType& type) override {
    return trampoline.Bounce(cond_.value(), VMType::Bool())
        .Transform([type, true_case = true_case_, false_case = false_case_,
                    &trampoline](EvaluationOutput cond_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (cond_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(cond_output)));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline.Bounce(cond_output.value.ptr()->get_bool()
                                           ? true_case.value()
                                           : false_case.value(),
                                       type);
          }
          language::Error error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<IfExpression>(cond_, true_case_, false_case_,
                                           return_types_);
  }

 private:
  const NonNull<std::shared_ptr<Expression>> cond_;
  const NonNull<std::shared_ptr<Expression>> true_case_;
  const NonNull<std::shared_ptr<Expression>> false_case_;
  const std::unordered_set<VMType> return_types_;
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
    compilation->AddError(
        L"Expected bool value for condition of \"if\" expression but found " +
        TypesToString(condition->Types()) + L".");
    return nullptr;
  }

  if (!(true_case->Types() == false_case->Types())) {
    compilation->AddError(
        L"Type mismatch between branches of conditional expression: " +
        TypesToString(true_case->Types()) + L" and " +
        TypesToString(false_case->Types()) + L".");
    return nullptr;
  }

  auto return_types =
      CombineReturnTypes(true_case->ReturnTypes(), false_case->ReturnTypes());
  if (return_types.IsError()) {
    compilation->AddError(return_types.error().description);
    return nullptr;
  }

  return std::make_unique<IfExpression>(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(condition)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(true_case)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(false_case)),
      std::move(return_types.value()));
}

}  // namespace afc::vm
