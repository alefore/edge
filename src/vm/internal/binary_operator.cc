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

void BinaryOperator::Evaluate(OngoingEvaluation* evaluation) {
  EvaluateExpression(evaluation, a_.get(),
      [this, evaluation](std::unique_ptr<Value> a_value) {
        // TODO: Remove shared_ptr when we can correctly capture a unique_ptr.
        std::shared_ptr<Value> a_value_shared(std::move(a_value));
        EvaluateExpression(evaluation, b_.get(),
            [this, evaluation, a_value_shared](std::unique_ptr<Value> b_value) {
              std::unique_ptr<Value> output(new Value(type_));
              operator_(*a_value_shared, *b_value, output.get());
              evaluation->consumer(std::move(output));
            });
      });
}

}  // namespace vm
}  // namespace afc
