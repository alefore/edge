#include "binary_operator.h"

#include "../internal/evaluation.h"
#include "../public/value.h"

namespace afc {
namespace vm {

BinaryOperator::BinaryOperator(
    unique_ptr<Expression> a, unique_ptr<Expression> b, const VMType type,
    function<void(const Value&, const Value&, Value*)> callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

const VMType& BinaryOperator::type() { return type_; }

void BinaryOperator::Evaluate(Trampoline* trampoline) {
  trampoline->Bounce(a_.get(),
      [this](std::unique_ptr<Value> a_value, Trampoline* trampoline) {
        // TODO: Remove shared_ptr when we can correctly capture a unique_ptr.
        std::shared_ptr<Value> a_value_shared(std::move(a_value));
        trampoline->Bounce(b_.get(),
            [this, a_value_shared](std::unique_ptr<Value> b_value,
                                   Trampoline* trampoline) {
              auto output = std::make_unique<Value>(type_);
              operator_(*a_value_shared, *b_value, output.get());
              trampoline->Continue(std::move(output));
            });
      });
}

}  // namespace vm
}  // namespace afc
