#ifndef __AFC_VM_FUNCTION_CALL_H__
#define __AFC_VM_FUNCTION_CALL_H__

#include <memory>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "value.h"

namespace afc {
namespace vm {

class Expression;
struct Compilation;

language::gc::Root<Expression> NewFunctionCall(
    language::gc::Ptr<Expression> func,
    std::vector<language::gc::Ptr<Expression>> args);

language::ValueOrError<language::gc::Root<Expression>> NewFunctionCall(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> func,
    std::vector<language::gc::Ptr<Expression>> args);

futures::ValueOrError<language::gc::Root<Value>> Call(
    language::gc::Pool& pool, const Value& func,
    std::vector<language::gc::Root<Value>> args,
    std::function<void(language::OnceOnlyFunction<void()>)> yield_callback);

language::ValueOrError<language::gc::Root<Expression>> NewMethodLookup(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> object,
    Identifier method_name);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_FUNCTION_CALL_H__
