#include "src/vm/constant_expression.h"

#include <glog/logging.h>

#include "src/vm/expression.h"
#include "src/vm/value.h"

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

  std::vector<Type> Types() override { return {value_.ptr()->type()}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline&,
                                                   const Type& type) override {
    CHECK(type == value_.ptr()->type());
    DVLOG(5) << "Evaluating constant value: " << value_.ptr().value();
    return futures::Past(EvaluationOutput::New(value_));
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const override {
    return {};
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
