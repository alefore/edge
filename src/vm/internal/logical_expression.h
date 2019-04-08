#ifndef __AFC_VM_LOGICAL_EXPRESSION_H__
#define __AFC_VM_LOGICAL_EXPRESSION_H__

#include <memory>

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Compilation;

unique_ptr<Expression> NewLogicalExpression(Compilation* compilation,
                                            bool identity,
                                            unique_ptr<Expression> a,
                                            unique_ptr<Expression> b);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_LOGICAL_EXPRESSION_H__
