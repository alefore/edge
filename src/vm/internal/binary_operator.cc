#include "src/vm/internal/binary_operator.h"

#include <glog/logging.h>

#include "src/language/error/value_or_error.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::math::numbers::Number;
using afc::math::numbers::ToInt;

namespace afc::vm {
BinaryOperator::BinaryOperator(NonNull<std::shared_ptr<Expression>> a,
                               NonNull<std::shared_ptr<Expression>> b,
                               const Type type,
                               function<ValueOrError<gc::Root<Value>>(
                                   gc::Pool& pool, const Value&, const Value&)>
                                   callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

std::vector<Type> BinaryOperator::Types() { return {type_}; }

std::unordered_set<Type> BinaryOperator::ReturnTypes() const {
  // TODO(easy): Should take b into account. That means changing cpp.y to be
  // able to handle errors here.
  return a_->ReturnTypes();
}

PurityType BinaryOperator::purity() {
  return a_->purity() == PurityType::kPure && b_->purity() == PurityType::kPure
             ? PurityType::kPure
             : PurityType::kUnknown;
}

futures::ValueOrError<EvaluationOutput> BinaryOperator::Evaluate(
    Trampoline& trampoline, const Type& type) {
  CHECK(type_ == type);
  return trampoline.Bounce(a_, a_->Types()[0])
      .Transform([b = b_, type = type_, op = operator_,
                  &trampoline](EvaluationOutput a_value) {
        return trampoline.Bounce(b, b->Types()[0])
            .Transform([&trampoline, a_value = std::move(a_value.value), type,
                        op](EvaluationOutput b_value)
                           -> ValueOrError<EvaluationOutput> {
              DECLARE_OR_RETURN(gc::Root<Value> result,
                                op(trampoline.pool(), a_value.ptr().value(),
                                   b_value.value.ptr().value()));
              CHECK(result.ptr()->type == type);
              return Success(EvaluationOutput::New(std::move(result)));
            });
      });
}

std::unique_ptr<Expression> NewBinaryExpression(
    Compilation* compilation, std::unique_ptr<Expression> a_raw,
    std::unique_ptr<Expression> b_raw,
    std::function<language::ValueOrError<wstring>(wstring, wstring)>
        str_operator,
    std::function<language::ValueOrError<math::numbers::Number>(
        math::numbers::Number, math::numbers::Number)>
        number_operator,
    std::function<language::ValueOrError<wstring>(wstring, int)>
        str_int_operator) {
  if (a_raw == nullptr || b_raw == nullptr) {
    return nullptr;
  }

  auto a = NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a_raw));
  auto b = NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b_raw));

  if (str_operator != nullptr && a->IsString() && b->IsString()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), types::String{},
        [str_operator](gc::Pool& pool, const Value& value_a,
                       const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(
              std::wstring value,
              str_operator(value_a.get_string(), value_b.get_string()));
          return Value::NewString(pool, std::move(value));
        });
  }

  if (number_operator != nullptr && a->IsNumber() && b->IsNumber()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), types::Number{},
        [number_operator](
            gc::Pool& pool, const Value& value_a,
            const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(
              Number value,
              number_operator(value_a.get_number(), value_b.get_number()));
          return Value::NewNumber(pool, value);
        });
  }

  if (str_int_operator != nullptr && a->IsString() && b->IsNumber()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), types::String{},
        [str_int_operator](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          DECLARE_OR_RETURN(int b_value_int, ToInt(b_value.get_number()));
          DECLARE_OR_RETURN(
              std::wstring value,
              str_int_operator(a_value.get_string(), b_value_int));
          return Value::NewString(pool, std::move(value));
        });
  }

  compilation->AddError(Error(L"Unable to add types: " +
                              TypesToString(a->Types()) + L" + " +
                              TypesToString(b->Types())));
  return nullptr;
}

}  // namespace afc::vm
