#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

namespace afc {
namespace vm {

// TODO(easy, 2023-10-04): Get rid of these fwd-declarations.
class Environment;
class Evaluation;

class Expression;
struct EvaluationOutput;

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
CompileFile(infrastructure::Path path, language::gc::Pool& pool,
            language::gc::Root<Environment> environment);

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
CompileString(const std::wstring& str, language::gc::Pool& pool,
              language::gc::Root<Environment> environment);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
