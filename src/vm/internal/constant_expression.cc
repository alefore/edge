#include "../public/constant_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

namespace gc = language::gc;

class ConstantExpression : public Expression {
 public:
  ConstantExpression(gc::Root<Value> value) : value_(std::move(value)) {}

  std::vector<VMType> Types() { return {value_.ptr()->type}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline&,
                                                   const VMType& type) {
    CHECK_EQ(type, value_.ptr()->type);
    DVLOG(5) << "Evaluating constant value: " << value_.ptr().value();
    return futures::Past(EvaluationOutput::New(value_));
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<ConstantExpression>(value_);
  }

 private:
  const gc::Root<Value> value_;
};

}  // namespace

NonNull<std::unique_ptr<Expression>> NewVoidExpression(gc::Pool& pool) {
  return NewConstantExpression(Value::NewVoid(pool));
}

NonNull<std::unique_ptr<Expression>> NewConstantExpression(
    gc::Root<Value> value) {
  return MakeNonNullUnique<ConstantExpression>(std::move(value));
}
}  // namespace afc::vm
