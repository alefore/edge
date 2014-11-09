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
  auto advancer = evaluation->advancer;

  evaluation->advancer =
      [this, advancer](OngoingEvaluation* evaluation_after_a) {
        // TODO: Remove shared_ptr when we can correctly capture a unique_ptr.
        shared_ptr<Value> a_value_shared(evaluation_after_a->value.release());
        evaluation_after_a->advancer =
            [this, a_value_shared, advancer]
            (OngoingEvaluation* evaluation_after_b) {
              unique_ptr<Value> output(new Value(type_));
              operator_(*a_value_shared, *evaluation_after_b->value, output.get());
              evaluation_after_b->value = std::move(output);
              evaluation_after_b->advancer = advancer;
            };
        b_->Evaluate(evaluation_after_a);
      };
  a_->Evaluate(evaluation);
}

}  // namespace vm
}  // namespace afc
