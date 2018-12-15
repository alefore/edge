#ifndef __AFC_VM_FUNCTION_CALL_H__
#define __AFC_VM_FUNCTION_CALL_H__

#include <memory>
#include <vector>
#include "value.h"

namespace afc {
namespace vm {

using std::unique_ptr;
using std::vector;

class Expression;

std::unique_ptr<Expression> NewFunctionCall(
    std::unique_ptr<Expression> func,
    std::vector<std::unique_ptr<Expression>> args);

void Call(Value* func, vector<Value::Ptr> args,
          std::function<void(Value::Ptr)> consumer);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
