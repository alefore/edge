#include "namespace_expression.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/environment.h"
#include "../public/value.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::VisitPointer;

class NamespaceExpression : public Expression {
 public:
  NamespaceExpression(Environment::Namespace full_namespace,
                      NonNull<std::shared_ptr<Expression>> body)
      : namespace_(full_namespace), body_(std::move(body)) {}

  std::vector<VMType> Types() override { return body_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() { return body_->purity(); }

  futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const VMType& type) override {
    auto original_environment = trampoline.environment();
    auto namespace_environment =
        Environment::LookupNamespace(original_environment, namespace_);
    CHECK(namespace_environment != nullptr);
    trampoline.SetEnvironment(namespace_environment);

    return OnError(trampoline.Bounce(*body_, type)
                       .Transform([&trampoline, original_environment](
                                      EvaluationOutput output) {
                         trampoline.SetEnvironment(original_environment);
                         return Success(std::move(output));
                       }),
                   [&trampoline, original_environment](Error error) {
                     trampoline.SetEnvironment(original_environment);
                     return error;
                   });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<NamespaceExpression>(namespace_, body_);
  }

 private:
  const Environment::Namespace namespace_;
  const NonNull<std::shared_ptr<Expression>> body_;
};

}  // namespace

void StartNamespaceDeclaration(Compilation* compilation,
                               const std::wstring& name) {
  compilation->current_namespace.push_back(name);
  compilation->environment =
      Environment::NewNamespace(std::move(compilation->environment), name);
}

std::unique_ptr<Expression> NewNamespaceExpression(
    Compilation* compilation, std::unique_ptr<Expression> body) {
  auto current_namespace = compilation->current_namespace;
  compilation->current_namespace.pop_back();
  compilation->environment = compilation->environment->parent_environment();
  return VisitPointer(
      std::move(body),
      [&](NonNull<std::unique_ptr<Expression>> body)
          -> std::unique_ptr<Expression> {
        return std::make_unique<NamespaceExpression>(
            std::move(current_namespace), std::move(body));
      },
      [] { return std::unique_ptr<Expression>(); });
}

}  // namespace afc::vm
