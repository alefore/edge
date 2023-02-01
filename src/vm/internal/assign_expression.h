#ifndef __AFC_VM_ASSIGN_EXPRESSION_H__
#define __AFC_VM_ASSIGN_EXPRESSION_H__

#include <memory>
#include <optional>
#include <string>

#include "src/vm/public/types.h"

namespace afc::vm {

using std::unique_ptr;
using std::wstring;

class Compilation;
class Expression;

// Declares a new variable of a given type.
std::optional<Type> NewDefineTypeExpression(Compilation* compilation,
                                            wstring type, wstring symbol,
                                            std::optional<Type> default_type);

// Declares a new variable of a given type and gives it an initial value.
unique_ptr<Expression> NewDefineExpression(Compilation* compilation,
                                           wstring type, wstring symbol,
                                           unique_ptr<Expression> value);

// Returns an expression that assigns a given value to an existing variable.
unique_ptr<Expression> NewAssignExpression(Compilation* compilation,
                                           wstring symbol,
                                           unique_ptr<Expression> value);

}  // namespace afc::vm

#endif  // __AFC_VM_ASSIGN_EXPRESSION_H__
