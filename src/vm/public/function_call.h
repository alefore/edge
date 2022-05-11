#ifndef __AFC_VM_FUNCTION_CALL_H__
#define __AFC_VM_FUNCTION_CALL_H__

#include <memory>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "value.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Compilation;

language::NonNull<std::unique_ptr<Expression>> NewFunctionCall(
    language::NonNull<std::unique_ptr<Expression>> func,
    std::vector<language::NonNull<std::unique_ptr<Expression>>> args);

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation,
    language::NonNull<std::unique_ptr<Expression>> func,
    std::vector<language::NonNull<std::unique_ptr<Expression>>> args);

futures::ValueOrError<language::NonNull<std::unique_ptr<Value>>> Call(
    language::gc::Pool& pool, const Value& func,
    std::vector<language::NonNull<Value::Ptr>> args,
    std::function<void(std::function<void()>)> yield_callback);

std::unique_ptr<Expression> NewMethodLookup(Compilation* compilation,
                                            std::unique_ptr<Expression> object,
                                            wstring method_name);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
