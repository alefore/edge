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

pair<Expression::Continuation, unique_ptr<Value>> BinaryOperator::Evaluate(
    const Evaluation& evaluation) {
  return a_->Evaluate(Evaluation(evaluation, Continuation(
      [this, evaluation](unique_ptr<Value> a_value) {
        // TODO: Remove shared_ptr when we can correctly capture a unique_ptr.
        shared_ptr<Value> a_value_shared(a_value.release());
        return b_->Evaluate(Evaluation(evaluation, Continuation(
            [this, a_value_shared, evaluation](unique_ptr<Value> b_value) {
              unique_ptr<Value> output(new Value(type_));
              operator_(*a_value_shared.get(), *b_value, output.get());
              return std::move(
                  make_pair(evaluation.continuation, std::move(output)));
            })));
      })));
}

}  // namespace vm
}  // namespace afc
