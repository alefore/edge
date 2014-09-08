#ifndef __AFC_VM_RETURN_EXPRESSION_H__
#define __AFC_VM_RETURN_EXPRESSION_H__

#include "../public/vm.h"

namespace afc {
namespace vm {

unique_ptr<Expression> NewReturnExpression(unique_ptr<Expression> expr);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_RETURN_EXPRESSION_H__
