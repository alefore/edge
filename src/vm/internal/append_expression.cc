#include "append_expression.h"

#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class AppendExpression : public Expression {
 public:
  AppendExpression(std::shared_ptr<Expression> e0,
                   std::shared_ptr<Expression> e1)
      : e0_(std::move(e0)), e1_(std::move(e1)) {}

  std::vector<VMType> Types() override { return e1_->Types(); }

  void Evaluate(Trampoline* trampoline, const VMType& type) override {
    trampoline->Bounce(
        e0_.get(), e0_->Types()[0],
        [e0 = e0_, e1 = e1_, type](Value::Ptr, Trampoline* trampoline) {
          e1->Evaluate(trampoline, type);
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<AppendExpression>(e0_, e1_);
  }

 private:
  const std::shared_ptr<Expression> e0_;
  const std::shared_ptr<Expression> e1_;
};

}  // namespace

std::unique_ptr<Expression> NewAppendExpression(std::unique_ptr<Expression> a,
                                                std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  return std::make_unique<AppendExpression>(std::move(a), std::move(b));
}

}  // namespace vm
}  // namespace afc
