#ifndef __AFC_VM_BINARY_OPERATOR_H__
#define __AFC_VM_BINARY_OPERATOR_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/gc.h"
#include "src/language/errors/value_or_error.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Evaluation;
class Compilation;

class BinaryOperator : public Expression {
 public:
  BinaryOperator(language::NonNull<std::shared_ptr<Expression>> a,
                 language::NonNull<std::shared_ptr<Expression>> b,
                 const Type type,
                 function<language::ValueOrError<language::gc::Root<Value>>(
                     language::gc::Pool& pool, const Value&, const Value&)>
                     callback);

  std::vector<Type> Types() override;
  std::unordered_set<Type> ReturnTypes() const override;

  PurityType purity() override;

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& evaluation,
                                                   const Type& type) override;

  language::NonNull<std::unique_ptr<Expression>> Clone() override;

 private:
  const language::NonNull<std::shared_ptr<Expression>> a_;
  const language::NonNull<std::shared_ptr<Expression>> b_;
  Type type_;
  std::function<language::ValueOrError<language::gc::Root<Value>>(
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
    std::function<language::ValueOrError<int>(int, int)> int_operator,
    std::function<language::ValueOrError<double>(double, double)>
        double_operator,
    std::function<language::ValueOrError<wstring>(wstring, int)>
        str_int_operator);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_BINARY_OPERATOR_H__
