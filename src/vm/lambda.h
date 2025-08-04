#ifndef __AFC_VM_LAMBDA_H__
#define __AFC_VM_LAMBDA_H__

#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"

namespace afc::vm {

// Temporary type used during compilation of a function expression. On `New`,
// receives parameters from the function's declaration. These are used on
// `Build` once the body of the expression becomes known.
class UserFunction {
 private:
  Compilation& compilation_;
  std::optional<Identifier> name_;
  Type type_;
  language::NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names_;

 public:
  static std::unique_ptr<UserFunction> New(
      Compilation& compilation, Identifier return_type,
      std::optional<Identifier> name,
      std::unique_ptr<std::vector<std::pair<Type, Identifier>>> args);

  UserFunction(Compilation& compilation, std::optional<Identifier> name,
               Type type, std::vector<std::pair<Type, Identifier>> args);
  ~UserFunction();

  UserFunction(const UserFunction&) = delete;
  UserFunction(UserFunction&&) = delete;

  language::ValueOrError<language::gc::Root<Expression>> BuildExpression(
      language::gc::Ptr<Expression> body);

  // It is the caller's responsibility to register errors.
  language::ValueOrError<language::gc::Root<Value>> BuildValue(
      language::gc::Ptr<Expression> body);

  void Abort();

  const std::optional<Identifier>& name() const;
  const Type& type() const;
};
}  // namespace afc::vm

#endif  // __AFC_VM_LAMBDA_H__
