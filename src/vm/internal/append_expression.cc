#include "src/vm/internal/append_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

class AppendExpression : public Expression {
 public:
  AppendExpression(NonNull<std::shared_ptr<Expression>> e0,
                   NonNull<std::shared_ptr<Expression>> e1,
                   std::unordered_set<Type> return_types)
      : e0_(std::move(e0)), e1_(std::move(e1)), return_types_(return_types) {
    // Check that the optimization in NewAppendExpression is applied.
    CHECK(e0_->purity() != PurityType::kPure || !e0_->ReturnTypes().empty());
  }

  std::vector<Type> Types() override { return e1_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override { return PurityType::kUnknown; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type&) override {
    return trampoline.Bounce(e0_.value(), e0_->Types()[0])
        .Transform([&trampoline, e1 = e1_](EvaluationOutput e0_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (e0_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(std::move(e0_output));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline.Bounce(e1.value(), e1->Types()[0]);
          }
          language::Error error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<AppendExpression>(e0_, e1_, return_types_);
  }

 private:
  const NonNull<std::shared_ptr<Expression>> e0_;
  const NonNull<std::shared_ptr<Expression>> e1_;
  const std::unordered_set<Type> return_types_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewAppendExpression(
    Compilation* compilation, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return Error(L"Missing input.");
  }
  return compilation->RegisterErrors(NewAppendExpression(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b))));
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewAppendExpression(
    NonNull<std::unique_ptr<Expression>> a,
    NonNull<std::unique_ptr<Expression>> b) {
  if (a->purity() == PurityType::kPure && a->ReturnTypes().empty())
    return Success(std::move(b));
  ASSIGN_OR_RETURN(std::unordered_set<Type> return_types,
                   CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes()));
  return Success<NonNull<std::unique_ptr<Expression>>>(
      MakeNonNullUnique<AppendExpression>(std::move(a), std::move(b),
                                          std::move(return_types)));
}

}  // namespace afc::vm
