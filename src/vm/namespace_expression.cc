#include "src/vm/namespace_expression.h"

#include <glog/logging.h>

#include "src/vm/compilation.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::VisitPointer;

class NamespaceExpression : public Expression {
 public:
  NamespaceExpression(Namespace full_namespace,
                      NonNull<std::shared_ptr<Expression>> body)
      : namespace_(full_namespace), body_(std::move(body)) {}

  std::vector<Type> Types() override { return body_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() override { return body_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    language::gc::Root<Environment> original_environment =
        trampoline.environment();
    std::optional<language::gc::Root<Environment>> namespace_environment =
        Environment::LookupNamespace(original_environment.ptr(), namespace_);
    CHECK(namespace_environment.has_value());
    trampoline.SetEnvironment(*namespace_environment);

    return OnError(trampoline.Bounce(body_, type)
                       .Transform([&trampoline, original_environment](
                                      EvaluationOutput output) {
                         trampoline.SetEnvironment(original_environment);
                         return Success(std::move(output));
                       }),
                   [&trampoline, original_environment](Error error) {
                     trampoline.SetEnvironment(original_environment);
                     return futures::Past(error);
                   });
  }

 private:
  const Namespace namespace_;
  const NonNull<std::shared_ptr<Expression>> body_;
};

}  // namespace

void StartNamespaceDeclaration(Compilation& compilation,
                               const std::wstring& name) {
  // TODO(easy, 2023-12-22): Don't convert to Identifier here but in the caller.
  compilation.current_namespace.push_back(Identifier(name));
  compilation.environment = Environment::NewNamespace(
      compilation.environment.ptr(), Identifier(name));
}

std::unique_ptr<Expression> NewNamespaceExpression(
    Compilation& compilation, std::unique_ptr<Expression> body_ptr) {
  Namespace current_namespace = compilation.current_namespace;
  compilation.current_namespace.pop_back();
  CHECK(compilation.environment.ptr()->parent_environment().has_value());
  compilation.environment =
      compilation.environment.ptr()->parent_environment()->ToRoot();
  return VisitPointer(
      std::move(body_ptr),
      [&](NonNull<std::unique_ptr<Expression>> body) {
        return std::make_unique<NamespaceExpression>(
            std::move(current_namespace), std::move(body));
      },
      [] { return nullptr; });
}

}  // namespace afc::vm
