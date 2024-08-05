#include "src/vm/if_expression.h"

#include <glog/logging.h>

#include "src/vm/compilation.h"
#include "src/vm/value.h"

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

class IfExpression : public Expression {
 public:
  IfExpression(NonNull<std::shared_ptr<Expression>> cond,
               NonNull<std::shared_ptr<Expression>> true_case,
               NonNull<std::shared_ptr<Expression>> false_case,
               std::unordered_set<Type> return_types)
      : cond_(std::move(cond)),
        true_case_(std::move(true_case)),
        false_case_(std::move(false_case)),
        return_types_(std::move(return_types)) {
    CHECK(cond_->IsBool());
  }

  std::vector<Type> Types() override { return true_case_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override {
    return CombinePurityType(
        {cond_->purity(), true_case_->purity(), false_case_->purity()});
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    return trampoline.Bounce(cond_, types::Bool{})
        .Transform([type, true_case = true_case_, false_case = false_case_,
                    &trampoline](EvaluationOutput cond_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (cond_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(cond_output)));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline.Bounce(
                  cond_output.value.ptr()->get_bool() ? true_case : false_case,
                  type);
          }
          language::Error error{LazyString{L"Unhandled OutputType case."}};
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

 private:
  const NonNull<std::shared_ptr<Expression>> cond_;
  const NonNull<std::shared_ptr<Expression>> true_case_;
  const NonNull<std::shared_ptr<Expression>> false_case_;
  const std::unordered_set<Type> return_types_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewIfExpression(
    Compilation& compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> true_case,
    std::unique_ptr<Expression> false_case) {
  if (condition == nullptr || true_case == nullptr || false_case == nullptr) {
    return Error{LazyString{L"Missing input parameter"}};
  }

  if (!condition->IsBool()) {
    Error error{LazyString{L"Expected bool value for condition of \"if\" "
                           L"expression but found "} +
                TypesToString(condition->Types()) + LazyString{L"."}};
    compilation.AddError(error);
    return error;
  }

  if (!(true_case->Types() == false_case->Types())) {
    Error error{
        LazyString{
            L"Type mismatch between branches of conditional expression: "} +
        TypesToString(true_case->Types()) + LazyString{L" and "} +
        TypesToString(false_case->Types()) + LazyString{L"."}};
    compilation.AddError(error);
    return error;
  }

  ASSIGN_OR_RETURN(std::unordered_set<Type> return_types,
                   compilation.RegisterErrors(CombineReturnTypes(
                       true_case->ReturnTypes(), false_case->ReturnTypes())));

  return MakeNonNullUnique<IfExpression>(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(condition)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(true_case)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(false_case)),
      std::move(return_types));
}

}  // namespace afc::vm
