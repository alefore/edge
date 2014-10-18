#ifndef __AFC_VM_FUNCTION_CALL_H__
#define __AFC_VM_FUNCTION_CALL_H__

#include <memory>
#include <vector>

namespace afc {
namespace vm {

using std::unique_ptr;
using std::vector;

class Expression;

unique_ptr<Expression> NewFunctionCall(
    unique_ptr<Expression> func,
    unique_ptr<vector<unique_ptr<Expression>>> args);

}  // namespace
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
