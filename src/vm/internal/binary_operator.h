#ifndef __AFC_VM_BINARY_OPERATOR_H__
#define __AFC_VM_BINARY_OPERATOR_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/value_or_error.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Evaluation;
class Compilation;

class BinaryOperator : public Expression {
 public:
  BinaryOperator(
      language::NonNull<std::shared_ptr<Expression>> a,
      language::NonNull<std::shared_ptr<Expression>> b, const VMType type,
      function<language::PossibleError(const Value&, const Value&, Value*)>
          callback);

  std::vector<VMType> Types() override;
  std::unordered_set<VMType> ReturnTypes() const override;

  PurityType purity() override;

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline* evaluation,
                                                   const VMType& type) override;

  language::NonNull<std::unique_ptr<Expression>> Clone() override;

 private:
  const language::NonNull<std::shared_ptr<Expression>> a_;
  const language::NonNull<std::shared_ptr<Expression>> b_;
  VMType type_;
  std::function<language::PossibleError(const Value&, const Value&, Value*)>
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
