#include "binary_operator.h"

#include <glog/logging.h>

#include "src/language/value_or_error.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/value.h"

namespace afc {
namespace vm {
using language::MakeNonNullUnique;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ValueOrError;

BinaryOperator::BinaryOperator(
    NonNull<std::shared_ptr<Expression>> a,
    NonNull<std::shared_ptr<Expression>> b, const VMType type,
    function<PossibleError(const Value&, const Value&, Value*)> callback)
    : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

std::vector<VMType> BinaryOperator::Types() { return {type_}; }

std::unordered_set<VMType> BinaryOperator::ReturnTypes() const {
  // TODO(easy): Should take b into account. That means changing cpp.y to be
  // able to handle errors here.
  return a_->ReturnTypes();
}

Expression::PurityType BinaryOperator::purity() {
  return a_->purity() == PurityType::kPure && b_->purity() == PurityType::kPure
             ? PurityType::kPure
             : PurityType::kUnknown;
}

futures::ValueOrError<EvaluationOutput> BinaryOperator::Evaluate(
    Trampoline* trampoline, const VMType& type) {
  CHECK(type_ == type);
  return trampoline->Bounce(*a_, a_->Types()[0])
      .Transform([b = b_, type = type_, op = operator_,
                  trampoline](EvaluationOutput a_value) {
        return trampoline->Bounce(*b, b->Types()[0])
            .Transform(
                [a_value = std::make_shared<Value>(std::move(*a_value.value)),
                 type, op](EvaluationOutput b_value)
                    -> ValueOrError<EvaluationOutput> {
                  auto output = MakeNonNullUnique<Value>(type);
                  auto result = op(*a_value, *b_value.value, output.get());
                  if (result.IsError()) return result.error();
                  return Success(EvaluationOutput::New(std::move(output)));
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
        [str_operator](const Value& value_a, const Value& value_b,
                       Value* output) -> PossibleError {
          auto result = str_operator(value_a.str, value_b.str);
          if (result.IsError()) return result.error();
          output->str = std::move(result.value());
          return Success();
        });
  }

  if (int_operator != nullptr && a->IsInteger() && b->IsInteger()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Integer(),
        [int_operator](const Value& value_a, const Value& value_b,
                       Value* output) -> PossibleError {
          auto result = int_operator(value_a.integer, value_b.integer);
          if (result.IsError()) return result.error();
          output->integer = std::move(result.value());
          return Success();
        });
  }

  if (double_operator != nullptr && (a->IsInteger() || a->IsDouble()) &&
      (b->IsInteger() || b->IsDouble())) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::Double(),
        [double_operator](const Value& a, const Value& b,
                          Value* output) -> PossibleError {
          auto to_double = [](const Value& x) {
            if (x.type.type == VMType::VM_INTEGER) {
              return static_cast<double>(x.integer);
            } else if (x.type.type == VMType::VM_DOUBLE) {
              return x.double_value;
            } else {
              CHECK(false) << "Unexpected type: " << x.type;
              return 0.0;  // Silence warning: no return.
            }
          };
          auto result = double_operator(to_double(a), to_double(b));
          if (result.IsError()) return result.error();
          output->double_value = result.value();
          return Success();
        });
  }

  if (str_int_operator != nullptr && a->IsString() && b->IsInteger()) {
    return std::make_unique<BinaryOperator>(
        std::move(a), std::move(b), VMType::String(),
        [str_int_operator](const Value& value_a, const Value& value_b,
                           Value* output) -> PossibleError {
          auto result = str_int_operator(value_a.str, value_b.integer);
          if (result.IsError()) return result.error();
          output->str = std::move(result.value());
          return Success();
        });
  }

  // TODO: Find a way to support this.
  compilation->errors.push_back(L"Unable to add types" /*: \"" +
                                a->type().ToString() + L"\" + \"" +
                                b->type().ToString() + L"\""*/);
  return nullptr;
}

}  // namespace vm
}  // namespace afc
