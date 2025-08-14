#ifndef __AFC_VM_BINARY_OPERATOR_H__
#define __AFC_VM_BINARY_OPERATOR_H__

#include <functional>
#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"
#include "src/vm/expression.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Evaluation;
struct Compilation;

class BinaryOperator : public Expression {
 private:
  struct ConstructorAccessTag {};

  const language::gc::Ptr<Expression> a_;
  const language::gc::Ptr<Expression> b_;
  const Type type_;
  const std::unordered_set<Type> return_types_;
  const std::function<language::ValueOrError<language::gc::Root<Value>>(
      language::gc::Pool&, const Value&, const Value&)>
      operator_;

 public:
  static language::ValueOrError<language::gc::Root<Expression>> New(
      language::ValueOrError<language::gc::Ptr<Expression>> a,
      language::ValueOrError<language::gc::Ptr<Expression>> b, Type type,
      std::function<language::ValueOrError<language::gc::Root<Value>>(
          language::gc::Pool& pool, const Value&, const Value&)>
          callback);

  BinaryOperator(
      ConstructorAccessTag, language::gc::Ptr<Expression> a,
      language::gc::Ptr<Expression> b, Type type,
      std::unordered_set<Type> return_types,
      std::function<language::ValueOrError<language::gc::Root<Value>>(
          language::gc::Pool& pool, const Value&, const Value&)>
          callback);

  std::vector<Type> Types() override;
  std::unordered_set<Type> ReturnTypes() const override;

  PurityType purity() override;

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& evaluation,
                                                   const Type& type) override;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override;
};

// A convenience wrapper of BinaryOperator that combines primitive types
// according to the functions given.
language::ValueOrError<language::gc::Root<Expression>> NewBinaryExpression(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> a,
    language::ValueOrError<language::gc::Ptr<Expression>> b,
    std::function<language::ValueOrError<language::lazy_string::LazyString>(
        language::lazy_string::LazyString, language::lazy_string::LazyString)>
        str_operator,
    std::function<language::ValueOrError<math::numbers::Number>(
        math::numbers::Number, math::numbers::Number)>
        number_operator,
    std::function<language::ValueOrError<language::lazy_string::LazyString>(
        language::lazy_string::LazyString, int)>
        str_int_operator);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_BINARY_OPERATOR_H__
