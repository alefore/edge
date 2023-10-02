#ifndef __AFC_VM_BINARY_OPERATOR_H__
#define __AFC_VM_BINARY_OPERATOR_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Evaluation;
struct Compilation;

class BinaryOperator : public Expression {
 private:
  struct ConstructorAccessKey {};

 public:
  static language::ValueOrError<
      language::NonNull<std::unique_ptr<BinaryOperator>>>
  New(language::NonNull<std::shared_ptr<Expression>> a,
      language::NonNull<std::shared_ptr<Expression>> b, Type type,
      function<language::ValueOrError<language::gc::Root<Value>>(
          language::gc::Pool& pool, const Value&, const Value&)>
          callback);

  BinaryOperator(ConstructorAccessKey,
                 language::NonNull<std::shared_ptr<Expression>> a,
                 language::NonNull<std::shared_ptr<Expression>> b, Type type,
                 std::unordered_set<Type> return_types,
                 function<language::ValueOrError<language::gc::Root<Value>>(
                     language::gc::Pool& pool, const Value&, const Value&)>
                     callback);

  std::vector<Type> Types() override;
  std::unordered_set<Type> ReturnTypes() const override;

  PurityType purity() override;

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& evaluation,
                                                   const Type& type) override;

 private:
  const language::NonNull<std::shared_ptr<Expression>> a_;
  const language::NonNull<std::shared_ptr<Expression>> b_;
  const Type type_;
  const std::unordered_set<Type> return_types_;
  const std::function<language::ValueOrError<language::gc::Root<Value>>(
      language::gc::Pool&, const Value&, const Value&)>
      operator_;
};

// A convenience wrapper of BinaryOperator that combines primitive types
// according to the functions given.
std::unique_ptr<Expression> NewBinaryExpression(
    Compilation* compilation, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b,
    std::function<language::ValueOrError<wstring>(wstring, wstring)>
        str_operator,
    std::function<language::ValueOrError<math::numbers::Number>(
        math::numbers::Number, math::numbers::Number)>
        number_operator,
    std::function<language::ValueOrError<wstring>(wstring, int)>
        str_int_operator);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_BINARY_OPERATOR_H__
