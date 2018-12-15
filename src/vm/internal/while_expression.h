#ifndef __AFC_VM_WHILE_EXPRESSION_H__
#define __AFC_VM_WHILE_EXPRESSION_H__

#include <memory>

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Compilation;

unique_ptr<Expression> NewWhileExpression(Compilation* compilation,
                                          unique_ptr<Expression> cond,
                                          unique_ptr<Expression> body);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_WHILE_EXPRESSION_H__
