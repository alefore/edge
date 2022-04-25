#include "../public/constant_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

class ConstantExpression : public Expression {
 public:
  ConstantExpression(NonNull<std::unique_ptr<Value>> value)
      : value_(std::move(value)) {}

  std::vector<VMType> Types() { return {value_->type}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline*,
                                                   const VMType& type) {
    CHECK_EQ(type, value_->type);
    DVLOG(5) << "Evaluating constant value: " << *value_;
    return futures::Past(
        Success(EvaluationOutput::New(MakeNonNullUnique<Value>(*value_))));
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    // Maybe make value_ a shared_ptr<> and avoid the deep copy?
    return MakeNonNullUnique<ConstantExpression>(
        MakeNonNullUnique<Value>(*value_));
  }

 private:
  const NonNull<std::unique_ptr<Value>> value_;
};

}  // namespace

std::unique_ptr<Expression> NewVoidExpression() {
  return NewConstantExpression(Value::NewVoid());
}

std::unique_ptr<Expression> NewConstantExpression(
    NonNull<std::unique_ptr<Value>> value) {
  return std::make_unique<ConstantExpression>(std::move(value));
}
}  // namespace afc::vm
