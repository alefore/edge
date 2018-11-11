#include "append_expression.h"

#include "evaluation.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class AppendExpression : public Expression {
 public:
  AppendExpression(std::unique_ptr<Expression> e0,
                   std::unique_ptr<Expression> e1)
      : e0_(std::move(e0)), e1_(std::move(e1)) {}

  const VMType& type() { return e1_->type(); }

  void Evaluate(Trampoline* trampoline) {
    trampoline->Bounce(e0_.get(),
        [this](std::unique_ptr<Value>, Trampoline* trampoline) {
          e1_->Evaluate(trampoline);
        });
  }

 private:
  const std::unique_ptr<Expression> e0_;
  const std::unique_ptr<Expression> e1_;
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
