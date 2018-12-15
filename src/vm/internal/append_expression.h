#ifndef __AFC_VM_APPEND_EXPRESSION_H__
#define __AFC_VM_APPEND_EXPRESSION_H__

#include <memory>

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;

unique_ptr<Expression> NewAppendExpression(unique_ptr<Expression> a,
                                           unique_ptr<Expression> b);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_APPEND_EXPRESSION_H__
