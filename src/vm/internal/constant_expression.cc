#include "../public/constant_expression.h"

#include <cassert>

#include <glog/logging.h>

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

  void Evaluate(Trampoline* trampoline) {
    DVLOG(5) << "Evaluating constant value: " << *value_;
    trampoline->Continue(std::unique_ptr<Value>(new Value(*value_)));
  }

 private:
  const std::unique_ptr<Value> value_;
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
