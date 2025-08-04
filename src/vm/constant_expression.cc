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

  const gc::Ptr<Value> value_;

 public:
  static language::gc::Root<ConstantExpression> New(gc::Ptr<Value> value) {
    gc::Pool& pool = value.pool();
    return pool.NewRoot(language::MakeNonNullUnique<ConstantExpression>(
        ConstructorAccessTag{}, std::move(value)));
  }

  ConstantExpression(ConstructorAccessTag, gc::Ptr<Value> value)
      : value_(std::move(value)) {}

  std::vector<Type> Types() override { return {value_->type()}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline&,
                                                   const Type& type) override {
    CHECK(type == value_->type());
    DVLOG(5) << "Evaluating constant value: " << value_.value();
    return futures::Past(EvaluationOutput::New(value_.ToRoot()));
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {value_.object_metadata()};
  }
};

}  // namespace

gc::Root<Expression> NewVoidExpression(gc::Pool& pool) {
  return ConstantExpression::New(Value::NewVoid(pool).ptr());
}

// TODO(2025-08-01, trivial): Receive a gc::Ptr, not gc::Root.
gc::Root<Expression> NewConstantExpression(gc::Root<Value> value) {
  return ConstantExpression::New(std::move(value).ptr());
}
}  // namespace afc::vm
