#include "src/vm/if_expression.h"

#include <glog/logging.h>

#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
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

class IfExpression : public Expression {
  struct ConstructorAccessTag {};

  const gc::Ptr<Expression> cond_;
  const gc::Ptr<Expression> true_case_;
  const gc::Ptr<Expression> false_case_;
  const std::unordered_set<Type> return_types_;

 public:
  static language::gc::Root<IfExpression> New(
      gc::Ptr<Expression> cond, gc::Ptr<Expression> true_case,
      gc::Ptr<Expression> false_case, std::unordered_set<Type> return_types) {
    gc::Pool& pool = cond.pool();
    return pool.NewRoot(language::MakeNonNullUnique<IfExpression>(
        ConstructorAccessTag{}, std::move(cond), std::move(true_case),
        std::move(false_case), std::move(return_types)));
  }

  IfExpression(ConstructorAccessTag, gc::Ptr<Expression> cond,
               gc::Ptr<Expression> true_case, gc::Ptr<Expression> false_case,
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
        .Transform([type, true_case = true_case_.ToRoot(),
                    false_case = false_case_.ToRoot(),
                    &trampoline](EvaluationOutput cond_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (cond_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(cond_output)));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline.Bounce(cond_output.value.ptr()->get_bool()
                                           ? true_case.ptr()
                                           : false_case.ptr(),
                                       type);
          }
          language::Error error{LazyString{L"Unhandled OutputType case."}};
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {cond_.object_metadata(), true_case_.object_metadata(),
            false_case_.object_metadata()};
  }
};

}  // namespace

ValueOrError<gc::Root<Expression>> NewIfExpression(
    Compilation& compilation,
    ValueOrError<gc::Ptr<Expression>> condition_or_error,
    ValueOrError<gc::Ptr<Expression>> true_case_or_error,
    ValueOrError<gc::Ptr<Expression>> false_case_or_error) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> condition, condition_or_error);
  DECLARE_OR_RETURN(gc::Ptr<Expression> true_case, true_case_or_error);
  DECLARE_OR_RETURN(gc::Ptr<Expression> false_case, false_case_or_error);

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

  return IfExpression::New(std::move(condition), std::move(true_case),
                           std::move(false_case), std::move(return_types));
}

}  // namespace afc::vm
