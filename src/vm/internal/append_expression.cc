#include "append_expression.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"

namespace afc::vm {
namespace {
using language::Success;

class AppendExpression : public Expression {
 public:
  AppendExpression(std::shared_ptr<Expression> e0,
                   std::shared_ptr<Expression> e1,
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

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                                   const VMType&) override {
    return trampoline->Bounce(e0_.get(), e0_->Types()[0])
        .Transform([trampoline, e1 = e1_](EvaluationOutput e0_output) {
          switch (e0_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(Success(std::move(e0_output)));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline->Bounce(e1.get(), e1->Types()[0]);
          }
          LOG(FATAL) << "Unhandled evaluation type";
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<AppendExpression>(e0_, e1_, return_types_);
  }

 private:
  const std::shared_ptr<Expression> e0_;
  const std::shared_ptr<Expression> e1_;
  const std::unordered_set<VMType> return_types_;
};

}  // namespace

std::unique_ptr<Expression> NewAppendExpression(Compilation* compilation,
                                                std::unique_ptr<Expression> a,
                                                std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  if (a->purity() == Expression::PurityType::kPure && a->ReturnTypes().empty())
    return b;
  std::wstring error;
  auto return_types =
      CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes(), &error);
  if (!return_types.has_value()) {
    compilation->errors.push_back(L"Incompatible return types found: " +
                                  TypesToString(a->ReturnTypes()) + L" and " +
                                  TypesToString(b->ReturnTypes()));
    return nullptr;
  }

  return std::make_unique<AppendExpression>(std::move(a), std::move(b),
                                            std::move(return_types.value()));
}

}  // namespace afc::vm
