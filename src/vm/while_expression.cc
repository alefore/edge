#include "while_expression.h"

#include <glog/logging.h>

#include "append_expression.h"
#include "compilation.h"
#include "src/language/overload.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitCallback;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

class WhileExpression : public Expression {
  struct ConstructorAccessTag {};

 public:
  static language::gc::Root<WhileExpression> New(
      language::gc::Pool& pool, NonNull<std::shared_ptr<Expression>> condition,
      NonNull<std::shared_ptr<Expression>> body) {
    return pool.NewRoot(
        language::MakeNonNullUnique<WhileExpression>(condition, body));
  }

  WhileExpression(NonNull<std::shared_ptr<Expression>> condition,
                  NonNull<std::shared_ptr<Expression>> body)
      : condition_(std::move(condition)), body_(std::move(body)) {}

  std::vector<Type> Types() override { return {types::Void{}}; }

  std::unordered_set<Type> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  PurityType purity() override {
    return CombinePurityType({condition_->purity(), body_->purity()});
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type&) override {
    DVLOG(4) << "Starting iteration.";
    return Iterate(trampoline, condition_, body_);
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  static futures::ValueOrError<EvaluationOutput> Iterate(
      Trampoline& trampoline, NonNull<std::shared_ptr<Expression>> condition,
      NonNull<std::shared_ptr<Expression>> body) {
    return trampoline.Bounce(condition, types::Bool{})
        .Transform([condition, body,
                    &trampoline](EvaluationOutput condition_output)
                       -> futures::ValueOrError<EvaluationOutput> {
          switch (condition_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return futures::Past(std::move(condition_output));

            case EvaluationOutput::OutputType::kContinue:
              if (!condition_output.value.ptr()->get_bool()) {
                DVLOG(3) << "Iteration is done.";
                return futures::Past(
                    EvaluationOutput::New(Value::NewVoid(trampoline.pool())));
              }

              DVLOG(5) << "Iterating...";
              return trampoline.Bounce(body, body->Types()[0])
                  .Transform([condition, body,
                              &trampoline](EvaluationOutput body_output)
                                 -> futures::ValueOrError<EvaluationOutput> {
                    switch (body_output.type) {
                      case EvaluationOutput::OutputType::kReturn:
                        return futures::Past(std::move(body_output));
                        break;
                      case EvaluationOutput::OutputType::kContinue:
                        return Iterate(trampoline, std::move(condition),
                                       std::move(body));
                    }
                    LOG(FATAL) << "Error: Unsupported EvaluationOutput type.";
                    return futures::Past(Error{LazyString{L"Internal error."}});
                  });
          }
          LOG(FATAL) << "Error: Unsupported EvaluationOutput type.";
          return futures::Past(Error{LazyString{L"Internal error."}});
        });
  }

  const NonNull<std::shared_ptr<Expression>> condition_;
  const NonNull<std::shared_ptr<Expression>> body_;
};

}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewWhileExpression(
    Compilation& compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> body) {
  if (condition == nullptr || body == nullptr) {
    return Error{LazyString{L"Input missing."}};
  }
  if (!condition->IsBool()) {
    Error error{LazyString{L"Expected bool value for condition of \"while\" "
                           L"loop but found: "} +
                TypesToString(condition->Types()) + LazyString{L"."}};
    compilation.AddError(error);
    return error;
  }

  return MakeNonNullUnique<WhileExpression>(
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(condition)),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(body)));
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> NewForExpression(
    Compilation& compilation, std::unique_ptr<Expression> init,
    std::unique_ptr<Expression> condition, std::unique_ptr<Expression> update,
    std::unique_ptr<Expression> body) {
  if (init == nullptr || condition == nullptr || update == nullptr ||
      body == nullptr) {
    return Error{LazyString{L"Input missing."}};
  }
  ASSIGN_OR_RETURN(
      language::gc::Root<Expression> body_expression_root,
      NewAppendExpression(
          compilation,
          compilation.pool.NewRoot(
              NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(body))),
          compilation.pool.NewRoot(NonNull<std::unique_ptr<Expression>>::Unsafe(
              std::move(update)))));
  NonNull<std::unique_ptr<Expression>> body_expression =
      NewDelegatingExpression(std::move(body_expression_root));

  ASSIGN_OR_RETURN(NonNull<std::unique_ptr<Expression>> while_expression,
                   NewWhileExpression(compilation, std::move(condition),
                                      std::move(body_expression).get_unique()));

  ASSIGN_OR_RETURN(
      language::gc::Root<Expression> result_root,
      NewAppendExpression(
          compilation,
          compilation.pool.NewRoot(
              NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(init))),
          compilation.pool.NewRoot(std::move(while_expression))));
  return NewDelegatingExpression(std::move(result_root));
}
}  // namespace afc::vm
