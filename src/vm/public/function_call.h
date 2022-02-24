#ifndef __AFC_VM_FUNCTION_CALL_H__
#define __AFC_VM_FUNCTION_CALL_H__

#include <memory>
#include <vector>

#include "src/futures/futures.h"
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

// TODO: Remove the nullptr default value and force all callers to pass a value.
futures::ValueOrError<std::unique_ptr<Value>> Call(
    const Value& func, vector<Value::Ptr> args,
    std::function<void(std::function<void()>)> yield_callback = nullptr);

std::unique_ptr<Expression> NewMethodLookup(Compilation* compilation,
                                            std::unique_ptr<Expression> object,
                                            wstring method_name);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
