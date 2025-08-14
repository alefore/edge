#include "src/vm/namespace_expression.h"

#include <glog/logging.h>

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::futures::OnError;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

class NamespaceExpression : public Expression {
  struct ConstructorAccessTag {};

  const Namespace namespace_;
  const gc::Ptr<Expression> body_;

 public:
  static language::gc::Root<NamespaceExpression> New(Namespace full_namespace,
                                                     gc::Ptr<Expression> body) {
    return body.pool().NewRoot(MakeNonNullUnique<NamespaceExpression>(
        ConstructorAccessTag{}, full_namespace, body));
  }

  NamespaceExpression(ConstructorAccessTag, Namespace full_namespace,
                      gc::Ptr<Expression> body)
      : namespace_(full_namespace), body_(std::move(body)) {}

  std::vector<Type> Types() override { return body_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() override { return body_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    language::gc::Root<Environment> original_environment =
        trampoline.environment().ToRoot();
    std::optional<language::gc::Root<Environment>> namespace_environment =
        Environment::LookupNamespace(original_environment.ptr(), namespace_);
    CHECK(namespace_environment.has_value());
    trampoline.SetEnvironment(namespace_environment->ptr());

    return OnError(trampoline.Bounce(body_, type)
                       .Transform([&trampoline, original_environment](
                                      EvaluationOutput output) {
                         trampoline.SetEnvironment(original_environment.ptr());
                         return Success(std::move(output));
                       }),
                   [&trampoline, original_environment](Error error) {
                     trampoline.SetEnvironment(original_environment.ptr());
                     return futures::Past(error);
                   });
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {body_.object_metadata()};
  }
};
}  // namespace

void StartNamespaceDeclaration(Compilation& compilation,
                               const Identifier& name) {
  compilation.current_namespace.push_back(name);
  compilation.environment = Environment::NewNamespace(
      compilation.environment.ptr(), Identifier(name));
}

language::ValueOrError<gc::Root<Expression>> NewNamespaceExpression(
    Compilation& compilation, std::optional<gc::Root<Expression>> body_ptr) {
  Namespace current_namespace = compilation.current_namespace;
  compilation.current_namespace.pop_back();
  CHECK(compilation.environment.ptr()->parent_environment().has_value());
  compilation.environment =
      compilation.environment.ptr()->parent_environment()->ToRoot();
  return VisitPointer(
      std::move(body_ptr),
      [&](gc::Root<Expression> body) -> ValueOrError<gc::Root<Expression>> {
        return NamespaceExpression::New(std::move(current_namespace),
                                        std::move(body).ptr());
      },
      [] -> ValueOrError<gc::Root<Expression>> {
        return Error{LazyString{L"Missing input."}};
      });
}

}  // namespace afc::vm
