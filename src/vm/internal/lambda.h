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

  std::unique_ptr<Expression> BuildExpression(Compilation* compilation,
                                              std::unique_ptr<Expression> body,
                                              std::wstring* error);

  std::unique_ptr<Value> BuildValue(Compilation* compilation,
                                    std::unique_ptr<Expression> body,
                                    std::wstring* error);
  void Abort(Compilation* compilation);
  void Done(Compilation* compilation);

  std::optional<std::wstring> name;
  VMType type;
  std::shared_ptr<std::vector<std::wstring>> argument_names =
      std::make_shared<std::vector<std::wstring>>();
};
}  // namespace afc::vm

#endif  // __AFC_VM_LAMBDA_H__
