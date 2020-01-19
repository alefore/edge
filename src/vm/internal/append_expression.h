#ifndef __AFC_VM_APPEND_EXPRESSION_H__
#define __AFC_VM_APPEND_EXPRESSION_H__

#include <memory>

namespace afc {
namespace vm {

class Expression;
class Compilation;

std::unique_ptr<Expression> NewAppendExpression(Compilation* compilation,
                                                std::unique_ptr<Expression> a,
                                                std::unique_ptr<Expression> b);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_APPEND_EXPRESSION_H__
