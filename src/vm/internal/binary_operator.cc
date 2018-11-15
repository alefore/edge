#include "binary_operator.h"

#include "../public/value.h"

namespace afc {
namespace vm {

BinaryOperator::BinaryOperator(
    unique_ptr<Expression> a, unique_ptr<Expression> b, const VMType type,
    function<void(const Value&, const Value&, Value*)> callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {
  CHECK(a_ != nullptr);
  CHECK(b_ != nullptr);
}

const VMType& BinaryOperator::type() { return type_; }

void BinaryOperator::Evaluate(Trampoline* trampoline) {
  // TODO: Bunch of things here can be turned to unique_ptr.
  auto type_copy = type_;
  auto operator_copy = operator_;
  std::shared_ptr<Expression> b_shared = b_;

  trampoline->Bounce(a_.get(),
      [b_shared, type_copy, operator_copy](
          std::unique_ptr<Value> a_value, Trampoline* trampoline) {
        std::shared_ptr<Value> a_value_shared(std::move(a_value));
        trampoline->Bounce(b_shared.get(),
            [a_value_shared, type_copy, operator_copy](
                std::unique_ptr<Value> b_value, Trampoline* trampoline) {
              auto output = std::make_unique<Value>(type_copy);
              operator_copy(*a_value_shared, *b_value, output.get());
              trampoline->Continue(std::move(output));
            });
      });
}

std::unique_ptr<Expression> BinaryOperator::Clone() {
  return std::make_unique<BinaryOperator>(
      a_->Clone(), b_->Clone(), type_, operator_);
}


}  // namespace vm
}  // namespace afc
