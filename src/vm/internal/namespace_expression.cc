#include "namespace_expression.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/environment.h"
#include "../public/value.h"

namespace afc::vm {
namespace {

class NamespaceExpression : public Expression {
 public:
  NamespaceExpression(Environment::Namespace full_namespace,
                      std::shared_ptr<Expression> body)
      : namespace_(full_namespace), body_(std::move(body)) {
    CHECK(body_ != nullptr);
  }

  std::vector<VMType> Types() override { return body_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() { return body_->purity(); }

  futures::Value<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                            const VMType& type) override {
    auto original_environment = trampoline->environment();
    auto namespace_environment =
        Environment::LookupNamespace(original_environment, namespace_);
    CHECK(namespace_environment != nullptr);
    trampoline->SetEnvironment(namespace_environment);

    return futures::Transform(
        trampoline->Bounce(body_.get(), type),
        [trampoline, original_environment](EvaluationOutput output) {
          trampoline->SetEnvironment(original_environment);
          return output;
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<NamespaceExpression>(namespace_, body_);
  }

 private:
  const Environment::Namespace namespace_;
  const std::shared_ptr<Expression> body_;
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
  if (body == nullptr) {
    return nullptr;
  }
  return std::make_unique<NamespaceExpression>(std::move(current_namespace),
                                               std::move(body));
}

}  // namespace afc::vm
