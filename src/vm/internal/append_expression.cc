#include "append_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

class AppendExpression : public Expression {
 public:
  AppendExpression(NonNull<std::shared_ptr<Expression>> e0,
                   NonNull<std::shared_ptr<Expression>> e1,
                   std::unordered_set<VMType> return_types)
      : e0_(std::move(e0)), e1_(std::move(e1)), return_types_(return_types) {
    // Check that the optimization in NewAppendExpression is applied.
    CHECK(e0_->purity() != PurityType::kPure || !e0_->ReturnTypes().empty());
  }

  std::vector<VMType> Types() override { return e1_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override { return PurityType::kUnknown; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const VMType&) override {
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
  const std::unordered_set<VMType> return_types_;
};

}  // namespace

std::unique_ptr<Expression> NewAppendExpression(Compilation* compilation,
                                                std::unique_ptr<Expression> a,
                                                std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  auto result = NewAppendExpression(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)));
  if (result.IsError()) {
    compilation->errors.push_back(result.error().description);
    return nullptr;
  }
  return std::move(result.value().get_unique());
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewAppendExpression(
    NonNull<std::unique_ptr<Expression>> a,
    NonNull<std::unique_ptr<Expression>> b) {
  if (a->purity() == Expression::PurityType::kPure && a->ReturnTypes().empty())
    return Success(std::move(b));
  auto return_types = CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes());
  if (return_types.IsError()) {
    return return_types.error();
  }

  return Success<NonNull<std::unique_ptr<Expression>>>(
      MakeNonNullUnique<AppendExpression>(std::move(a), std::move(b),
                                          std::move(return_types.value())));
}

}  // namespace afc::vm
