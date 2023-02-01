#ifndef __AFC_VM_LAMBDA_H__
#define __AFC_VM_LAMBDA_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/gc.h"

namespace afc::vm {
class Compilation;

// Temporary type used during compilation of a function expression. On `New`,
// receives parameters from the function's declaration. These are used on
// `Build` once the body of the expression becomes known.
struct UserFunction {
  static std::unique_ptr<UserFunction> New(
      Compilation& compilation, std::wstring return_type,
      std::optional<std::wstring> name,
      std::vector<std::pair<Type, wstring>>* args);

  language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
  BuildExpression(Compilation& compilation,
                  language::NonNull<std::unique_ptr<Expression>> body);

  // It is the caller's responsibility to register errors.
  language::ValueOrError<language::gc::Root<Value>> BuildValue(
      Compilation& compilation,
      language::NonNull<std::unique_ptr<Expression>> body);
  void Abort(Compilation& compilation);
  void Done(Compilation& compilation);

  std::optional<std::wstring> name;
  Type type;
  language::NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names;
};
}  // namespace afc::vm

#endif  // __AFC_VM_LAMBDA_H__
