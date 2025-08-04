#include "src/vm/append_expression.h"

#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::vm::NewDelegatingExpression;

namespace gc = afc::language::gc;

namespace afc::vm {
namespace {

class AppendExpression : public Expression {
  struct ConstructorAccessTag {};

  const gc::Ptr<Expression> e0_;
  const gc::Ptr<Expression> e1_;
  const std::unordered_set<Type> return_types_;

 public:
  static gc::Root<AppendExpression> New(gc::Ptr<Expression> e0,
                                        gc::Ptr<Expression> e1,
                                        std::unordered_set<Type> return_types) {
    gc::Pool& pool = e0.pool();
    return pool.NewRoot(MakeNonNullUnique<AppendExpression>(
        ConstructorAccessTag{}, std::move(e0), std::move(e1),
        std::move(return_types)));
  }

  AppendExpression(ConstructorAccessTag, gc::Ptr<Expression> e0,
                   gc::Ptr<Expression> e1,
                   std::unordered_set<Type> return_types)
      : e0_(std::move(e0)), e1_(std::move(e1)), return_types_(return_types) {
    // Check that the optimization in NewAppendExpression is applied.
    CHECK(e0_->purity().writes_external_outputs ||
          e0_->purity().writes_local_variables || !e0_->ReturnTypes().empty());
  }

  std::vector<Type> Types() override { return e1_->Types(); }

  std::unordered_set<Type> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override {
    return CombinePurityType({e0_->purity(), e1_->purity()});
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type&) override {
    return trampoline
        .Bounce(NewDelegatingExpression(e0_.ToRoot()), e0_->Types()[0])
        .Transform([&trampoline, e1 = e1_.ToRoot()](EvaluationOutput e0_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (e0_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(std::move(e0_output));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline.Bounce(NewDelegatingExpression(e1),
                                       e1->Types()[0]);
          }
          Error error(LazyString{L"Unhandled OutputType case."});
          LOG(FATAL) << error;
          return futures::Past(error);
        });
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {e0_.object_metadata(), e1_.object_metadata()};
  }
};

}  // namespace

ValueOrError<gc::Root<Expression>> NewAppendExpression(
    Compilation& compilation, std::optional<gc::Ptr<Expression>> a,
    std::optional<gc::Ptr<Expression>> b) {
  if (!a.has_value() || !b.has_value()) {
    return Error(LazyString{L"Missing input."});
  }
  return compilation.RegisterErrors(NewAppendExpression(a.value(), b.value()));
}

ValueOrError<gc::Root<Expression>> NewAppendExpression(gc::Ptr<Expression> a,
                                                       gc::Ptr<Expression> b) {
  if (!a->purity().writes_external_outputs &&
      !a->purity().writes_local_variables && a->ReturnTypes().empty())
    return Success(b.ToRoot());
  ASSIGN_OR_RETURN(std::unordered_set<Type> return_types,
                   CombineReturnTypes(a->ReturnTypes(), b->ReturnTypes()));
  return Success<gc::Root<Expression>>(AppendExpression::New(
      std::move(a), std::move(b), std::move(return_types)));
}

}  // namespace afc::vm
