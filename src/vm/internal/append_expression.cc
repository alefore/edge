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

  const VMType& type() { return e1_->type(); }

  void Evaluate(Trampoline* trampoline) override {
    // TODO: Use unique_ptr and capture by std::move.
    std::shared_ptr<Expression> e0_copy = e0_;
    std::shared_ptr<Expression> e1_copy = e1_;
    trampoline->Bounce(e0_copy.get(),
        [e0_copy, e1_copy](std::unique_ptr<Value>, Trampoline* trampoline) {
          e1_copy->Evaluate(trampoline);
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

std::unique_ptr<Expression> NewAppendExpression(
    std::unique_ptr<Expression> a, std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  }
  return std::make_unique<AppendExpression>(std::move(a), std::move(b));
}

}  // namespace
}  // namespace afc
