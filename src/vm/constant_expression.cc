#include "src/vm/constant_expression.h"

#include <glog/logging.h>

#include "src/language/gc.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
namespace afc::vm {
namespace {

namespace gc = language::gc;

class ConstantExpression : public Expression {
  struct ConstructorAccessTag {};

 public:
  static language::gc::Root<ConstantExpression> New(language::gc::Pool& pool,
                                                    gc::Root<Value> value) {
    return pool.NewRoot(
        language::MakeNonNullUnique<ConstantExpression>(std::move(value)));
  }

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

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  const gc::Root<Value> value_;
};

}  // namespace

gc::Root<Expression> NewVoidExpression(gc::Pool& pool) {
  return NewConstantExpression(Value::NewVoid(pool));
}

// TODO(2025-08-01, trivial): Receive a gc::Ptr, not gc::Root.
gc::Root<Expression> NewConstantExpression(gc::Root<Value> value) {
  gc::Pool& pool = value.pool();
  return pool.NewRoot(MakeNonNullUnique<ConstantExpression>(std::move(value)));
}
}  // namespace afc::vm
