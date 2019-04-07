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
class Compilation;

std::unique_ptr<Expression> NewFunctionCall(
    std::unique_ptr<Expression> func,
    std::vector<std::unique_ptr<Expression>> args);

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation, std::unique_ptr<Expression> func,
    std::vector<std::unique_ptr<Expression>> args);

void Call(Value* func, vector<Value::Ptr> args,
          std::function<void(Value::Ptr)> consumer);

std::unique_ptr<Expression> NewMethodLookup(Compilation* compilation,
                                            std::unique_ptr<Expression> object,
                                            wstring method_name);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
