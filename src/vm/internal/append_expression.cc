#include "append_expression.h"

#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class AppendExpression : public Expression {
 public:
  AppendExpression(std::shared_ptr<Expression> e0,
                   std::shared_ptr<Expression> e1,
                   std::unordered_set<VMType> return_types)
      : e0_(std::move(e0)), e1_(std::move(e1)), return_types_(return_types) {}

  std::vector<VMType> Types() override { return e1_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return return_types_;
  }

  futures::DelayedValue<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                                   const VMType&) override {
    return futures::DelayedValue<EvaluationOutput>::Transform(
        trampoline->Bounce(e0_.get(), e0_->Types()[0]),
        [trampoline, e0 = e0_, e1 = e1_](EvaluationOutput e0_output) {
          if (e0_output.type == EvaluationOutput::OutputType::kReturn) {
            return futures::ImmediateValue(std::move(e0_output));
          }
          return futures::DelayedValue<EvaluationOutput>::ImmediateTransform(
              trampoline->Bounce(e1.get(), e1->Types()[0]),
              [e1](EvaluationOutput e1_output) {
                return e1_output;  // Keep `e1` alive.
              });
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

std::unique_ptr<Expression> NewAppendExpression(std::unique_ptr<Expression> a,
                                                std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  std::wstring error;
  auto return_types =
      CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes(), &error);
  if (!return_types.has_value()) {
    return nullptr;  // TODO(easy): Don't shallow the error.
  }

  return std::make_unique<AppendExpression>(std::move(a), std::move(b),
                                            std::move(return_types.value()));
}

}  // namespace vm
}  // namespace afc
