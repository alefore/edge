#ifndef __AFC_VM_BINARY_OPERATOR_H__
#define __AFC_VM_BINARY_OPERATOR_H__

#include <memory>
#include "../public/vm.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Evaluation;

class BinaryOperator : public Expression {
 public:
  BinaryOperator(unique_ptr<Expression> a, unique_ptr<Expression> b,
                 const VMType type,
                 function<void(const Value&, const Value&, Value*)> callback);

  const VMType& type();

  void Evaluate(Trampoline* evaluation);

  std::unique_ptr<Expression> Clone() override;

 private:
  std::shared_ptr<Expression> a_;
  std::shared_ptr<Expression> b_;
  VMType type_;
  std::function<void(const Value&, const Value&, Value*)> operator_;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_BINARY_OPERATOR_H__
