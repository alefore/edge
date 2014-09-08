#ifndef __AFC_VM_RETURN_EXPRESSION_H__
#define __AFC_VM_RETURN_EXPRESSION_H__

#include "../public/vm.h"

namespace afc {
namespace vm {

class Compilation;
class Expression;

unique_ptr<Expression> NewReturnExpression(
    Compilation* compilation, unique_ptr<Expression> expr);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_RETURN_EXPRESSION_H__
