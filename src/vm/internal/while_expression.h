#ifndef __AFC_VM_WHILE_EXPRESSION_H__
#define __AFC_VM_WHILE_EXPRESSION_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/language/error/value_or_error.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Compilation;

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
NewWhileExpression(Compilation* compilation, unique_ptr<Expression> cond,
                   unique_ptr<Expression> body);

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
NewForExpression(Compilation* compilation, std::unique_ptr<Expression> init,
                 std::unique_ptr<Expression> condition,
                 std::unique_ptr<Expression> update,
                 std::unique_ptr<Expression> body);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_WHILE_EXPRESSION_H__
