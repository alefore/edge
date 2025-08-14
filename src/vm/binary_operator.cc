#include "src/vm/binary_operator.h"

#include <glog/logging.h>

#include "src/language/error/value_or_error.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::math::numbers::Number;

namespace afc::vm {

/*static*/
ValueOrError<gc::Root<Expression>> BinaryOperator::New(
    ValueOrError<gc::Ptr<Expression>> a_or_error,
    ValueOrError<gc::Ptr<Expression>> b_or_error, Type type,
    std::function<ValueOrError<gc::Root<Value>>(gc::Pool& pool, const Value&,
                                                const Value&)>
        callback) {
  ASSIGN_OR_RETURN(gc::Ptr<Expression> a, a_or_error);
  ASSIGN_OR_RETURN(gc::Ptr<Expression> b, b_or_error);
  ASSIGN_OR_RETURN(std::unordered_set<Type> return_types,
                   CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes()));
  gc::Pool& pool = a.pool();
  return pool.NewRoot(MakeNonNullUnique<BinaryOperator>(
      ConstructorAccessTag{}, std::move(a), std::move(b), std::move(type),
      std::move(return_types), std::move(callback)));
}

BinaryOperator::BinaryOperator(ConstructorAccessTag, gc::Ptr<Expression> a,
                               gc::Ptr<Expression> b, Type type,
                               std::unordered_set<Type> return_types,
                               std::function<ValueOrError<gc::Root<Value>>(
                                   gc::Pool& pool, const Value&, const Value&)>
                                   callback)
    : a_(std::move(a)),
      b_(std::move(b)),
      type_(std::move(type)),
      return_types_(std::move(return_types)),
      operator_(std::move(callback)) {}

std::vector<Type> BinaryOperator::Types() { return {type_}; }

std::unordered_set<Type> BinaryOperator::ReturnTypes() const {
  return return_types_;
}

PurityType BinaryOperator::purity() {
  return CombinePurityType({a_->purity(), b_->purity()});
}

futures::ValueOrError<EvaluationOutput> BinaryOperator::Evaluate(
    Trampoline& trampoline, const Type& type) {
  CHECK(type_ == type);
  return trampoline.Bounce(a_, a_->Types()[0])
      .Transform([b = b_.ToRoot(), type = type_, op = operator_,
                  &trampoline](EvaluationOutput a_value) {
        return trampoline.Bounce(b.ptr(), b->Types()[0])
            .Transform([&trampoline, a_value = std::move(a_value.value), type,
                        op](EvaluationOutput b_value)
                           -> ValueOrError<EvaluationOutput> {
              DECLARE_OR_RETURN(gc::Root<Value> result,
                                op(trampoline.pool(), a_value.ptr().value(),
                                   b_value.value.ptr().value()));
              CHECK(result.ptr()->type() == type);
              return Success(EvaluationOutput::New(std::move(result)));
            });
      });
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BinaryOperator::Expand() const {
  return {a_.object_metadata(), b_.object_metadata()};
}

ValueOrError<gc::Root<Expression>> NewBinaryExpression(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> a_or_error,
    ValueOrError<gc::Ptr<Expression>> b_or_error,
    std::function<ValueOrError<LazyString>(LazyString, LazyString)>
        str_operator,
    std::function<ValueOrError<math::numbers::Number>(math::numbers::Number,
                                                      math::numbers::Number)>
        number_operator,
    std::function<ValueOrError<LazyString>(LazyString, int)> str_int_operator) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> a, a_or_error);
  DECLARE_OR_RETURN(gc::Ptr<Expression> b, b_or_error);

  if (str_operator != nullptr && a->IsString() && b->IsString()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a), std::move(b), types::String{},
        [str_operator](gc::Pool& pool, const Value& value_a,
                       const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(
              LazyString value,
              str_operator(value_a.get_string(), value_b.get_string()));
          return Value::NewString(pool, std::move(value));
        }));
  }

  if (number_operator != nullptr && a->IsNumber() && b->IsNumber()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a), std::move(b), types::Number{},
        [number_operator](
            gc::Pool& pool, const Value& value_a,
            const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(
              Number value,
              number_operator(value_a.get_number(), value_b.get_number()));
          return Value::NewNumber(pool, value);
        }));
  }

  if (str_int_operator != nullptr && a->IsString() && b->IsNumber()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a), std::move(b), types::String{},
        [str_int_operator](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(int b_value_int, b_value.get_number().ToInt32());
          DECLARE_OR_RETURN(
              LazyString value,
              str_int_operator(a_value.get_string(), b_value_int));
          return Value::NewString(pool, std::move(value));
        }));
  }

  return compilation.AddError(
      Error{LazyString{L"Unable to add types: "} + TypesToString(a->Types()) +
            LazyString{L" + "} + TypesToString(b->Types())});
}

}  // namespace afc::vm
