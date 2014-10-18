#ifndef __AFC_VM_INTERNAL_IF_EXPRESSION_H__
#define __AFC_VM_INTERNAL_IF_EXPRESSION_H__

#include <memory>
#include "../public/vm.h"

namespace afc {
namespace vm {

class Compilation;

using std::unique_ptr;

unique_ptr<Expression> NewIfExpression(
    Compilation* compilation,
    unique_ptr<Expression> condition,
    unique_ptr<Expression> true_case,
    unique_ptr<Expression> false_case);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_INTERNAL_IF_EXPRESSION_H__
