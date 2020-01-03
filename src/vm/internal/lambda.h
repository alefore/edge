#ifndef __AFC_VM_LAMBDA_H__
#define __AFC_VM_LAMBDA_H__

#include <memory>

#include "../public/vm.h"

namespace afc::vm {
class Compilation;

// Temporary type used during compilation of a function expression. On `New`,
// receives parameters from the function's declaration. These are used on
// `Build` once the body of the expression becomes known.
struct UserFunction {
  static std::unique_ptr<UserFunction> New(
      Compilation* compilation, std::wstring return_type,
      std::optional<std::wstring> name,
      std::vector<std::pair<VMType, wstring>>* args);

  std::unique_ptr<Value> Build(Compilation* compilation,
                               std::unique_ptr<Expression> body,
                               std::wstring* error);

  std::optional<std::wstring> name;
  VMType type;
  vector<wstring> argument_names;
};
}  // namespace afc::vm

#endif  // __AFC_VM_LAMBDA_H__
