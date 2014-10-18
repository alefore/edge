#include "constant_expression.h"

#include <cassert>

#include "evaluation.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class ConstantExpression : public Expression {
 public:
  ConstantExpression(unique_ptr<Value> value) : value_(std::move(value)) {
    assert(value_ != nullptr);
  }

  const VMType& type() { return value_->type; }

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    unique_ptr<Value> value(new Value(value_->type.type));
    *value = *value_;
    return make_pair(evaluation.continuation, std::move(value));
  }

 private:
  unique_ptr<Value> value_;
};

}  // namespace

unique_ptr<Expression> NewVoidExpression() {
  return unique_ptr<Expression>(new ConstantExpression(Value::NewVoid()));
}

unique_ptr<Expression> NewConstantExpression(unique_ptr<Value> value) {
  assert(value != nullptr);
  return unique_ptr<Expression>(new ConstantExpression(std::move(value)));
}


}  // namespace vm
}  // namespace afc
