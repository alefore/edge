#include "src/vm/delegating_expression.h"

#include <unordered_set>
#include <utility>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/error/log.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

using afc::futures::ValueOrError;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::gc::ObjectMetadata;
using afc::vm::EvaluationOutput;
using afc::vm::Trampoline;
using afc::vm::Type;

namespace gc = afc::language::gc;

namespace afc::vm {
namespace {
class DelegatingExpression : public Expression {
 public:
  explicit DelegatingExpression(gc::Root<Expression> delegate)
      : delegate_(std::move(delegate)) {}

  std::vector<Type> Types() override { return delegate_->Types(); }
  std::unordered_set<Type> ReturnTypes() const override {
    return delegate_->ReturnTypes();
  }
  PurityType purity() override { return delegate_->purity(); }
  ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                          const Type& type) override {
    return delegate_->Evaluate(trampoline, type);
  }
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand()
      const override {
    return {};  // Expand should be empty as gc::Root members are automatically
                // handled.
  }

 private:
  gc::Root<Expression> delegate_;
};
}  // namespace

NonNull<std::unique_ptr<Expression>> NewDelegatingExpression(
    gc::Root<Expression> delegate) {
  return MakeNonNullUnique<DelegatingExpression>(std::move(delegate));
}

}  // namespace afc::vm
