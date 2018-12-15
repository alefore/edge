#ifndef __AFC_VM_ASSIGN_EXPRESSION_H__
#define __AFC_VM_ASSIGN_EXPRESSION_H__

#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::unique_ptr;
using std::wstring;

class Compilation;
class Expression;

// Declares a new variable of a given type and gives it an initial value.
unique_ptr<Expression> NewAssignExpression(Compilation* compilation,
                                           const wstring& type,
                                           const wstring& symbol,
                                           unique_ptr<Expression> value);

// Returns an expression that assigns a given value to an existing variable.
unique_ptr<Expression> NewAssignExpression(Compilation* compilation,
                                           const wstring& symbol,
                                           unique_ptr<Expression> value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_ASSIGN_EXPRESSION_H__
