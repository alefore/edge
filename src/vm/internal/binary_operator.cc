#include "binary_operator.h"

#include <glog/logging.h>

#include "src/language/value_or_error.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/value.h"

namespace afc {
namespace vm {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ValueOrError;

namespace gc = language::gc;

BinaryOperator::BinaryOperator(NonNull<std::shared_ptr<Expression>> a,
                               NonNull<std::shared_ptr<Expression>> b,
                               const VMType type,
                               function<ValueOrError<gc::Root<Value>>(
                                   gc::Pool& pool, const Value&, const Value&)>
                                   callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

std::vector<VMType> BinaryOperator::Types() { return {type_}; }

std::unordered_set<VMType> BinaryOperator::ReturnTypes() const {
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
    Trampoline& trampoline, const VMType& type) {
  CHECK(type_ == type);
  return trampoline.Bounce(a_.value(), a_->Types()[0])
      .Transform([b = b_, type = type_, op = operator_,
                  &trampoline](EvaluationOutput a_value) {
        return trampoline.Bounce(b.value(), b->Types()[0])
            .Transform([&trampoline, a_value = std::move(a_value.value), type,
                        op](EvaluationOutput b_value)
                           -> ValueOrError<EvaluationOutput> {
              ASSIGN_OR_RETURN(gc::Root<Value> result,
                               op(trampoline.pool(), a_value.ptr().value(),
                                  b_value.value.ptr().value()));
              CHECK_EQ(result.ptr()->type, type);
              return Success(EvaluationOutput::New(std::move(result)));
            });
      });
}

NonNull<std::unique_ptr<Expression>> BinaryOperator::Clone() {
  return MakeNonNullUnique<BinaryOperator>(a_, b_, type_, operator_);
}

std::unique_ptr<Expression> NewBinaryExpression(
    Compilation* compilation, std::unique_ptr<Expression> a_raw,
    std::unique_ptr<Expression> b_raw,
    std::function<ValueOrError<wstring>(wstring, wstring)> str_operator,
    std::function<ValueOrError<int>(int, int)> int_operator,
    std::function<ValueOrError<double>(double, double)> double_operator,
    std::function<ValueOrError<wstring>(wstring, int)> str_int_operator) {
  if (a_raw == nullptr || b_raw == nullptr) {
    return nullptr;
  }

  auto a = NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a_raw));
  auto b = NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b_raw));

  if (str_operator != nullptr && a->IsString() && b->IsString()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::String(),
        [str_operator](gc::Pool& pool, const Value& value_a,
                       const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          ASSIGN_OR_RETURN(
              std::wstring value,
              str_operator(value_a.get_string(), value_b.get_string()));
          return Value::NewString(pool, std::move(value));
        });
  }

  if (int_operator != nullptr && a->IsInt() && b->IsInt()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Int(),
        [int_operator](gc::Pool& pool, const Value& value_a,
                       const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          ASSIGN_OR_RETURN(int value,
                           int_operator(value_a.get_int(), value_b.get_int()));
          return Value::NewInt(pool, value);
        });
  }

  if (double_operator != nullptr && (a->IsInt() || a->IsDouble()) &&
      (b->IsInt() || b->IsDouble())) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Double(),
        [double_operator](gc::Pool& pool, const Value& a,
                          const Value& b) -> ValueOrError<gc::Root<Value>> {
          auto to_double = [](const Value& x) {
            if (x.type == VMType::Int()) {
              return static_cast<double>(x.get_int());
            } else if (x.type == VMType::Double()) {
              return x.get_double();
            } else {
              CHECK(false) << "Unexpected type: " << x.type;
              return 0.0;  // Silence warning: no return.
            }
          };
          ASSIGN_OR_RETURN(double value,
                           double_operator(to_double(a), to_double(b)));
          return Value::NewDouble(pool, value);
        });
  }

  if (str_int_operator != nullptr && a->IsString() && b->IsInt()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::String(),
        [str_int_operator](
            gc::Pool& pool, const Value& value_a,
            const Value& value_b) -> ValueOrError<gc::Root<Value>> {
          ASSIGN_OR_RETURN(
              std::wstring value,
              str_int_operator(value_a.get_string(), value_b.get_int()));
          return Value::NewString(pool, std::move(value));
        });
  }

  // TODO(2022-05-29, easy): Find a way to support this.
  // TypesToString(a->Types())?
  compilation->AddError(Error(L"Unable to add types" /*: \"" +
                                a->type().ToString() + L"\" + \"" +
                                b->type().ToString() + L"\""*/));
  return nullptr;
}

}  // namespace vm
}  // namespace afc
