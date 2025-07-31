#include "while_expression.h"

#include <glog/logging.h>

#include "append_expression.h"
#include "compilation.h"
#include "src/language/overload.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
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

  const gc::Ptr<Expression> condition_;
  const gc::Ptr<Expression> body_;

 public:
  static language::gc::Root<WhileExpression> New(gc::Ptr<Expression> condition,
                                                 gc::Ptr<Expression> body) {
    return body.pool().NewRoot(language::MakeNonNullUnique<WhileExpression>(
        ConstructorAccessTag{}, std::move(condition), std::move(body)));
  }

  WhileExpression(ConstructorAccessTag, gc::Ptr<Expression> condition,
                  gc::Ptr<Expression> body)
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
    return Iterate(trampoline, condition_.ToRoot(), body_.ToRoot());
  }

  std::vector<NonNull<std::shared_ptr<language::gc::ObjectMetadata>>> Expand()
      const override {
    return {condition_.object_metadata(), body_.object_metadata()};
  }

 private:
  static futures::ValueOrError<EvaluationOutput> Iterate(
      Trampoline& trampoline, gc::Root<Expression> condition,
      gc::Root<Expression> body) {
    return trampoline.Bounce(NewDelegatingExpression(condition), types::Bool{})
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
              return trampoline
                  .Bounce(NewDelegatingExpression(body), body->Types()[0])
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
};

}  // namespace

ValueOrError<gc::Root<Expression>> NewWhileExpression(
    Compilation& compilation, std::optional<gc::Root<Expression>> condition,
    std::optional<gc::Root<Expression>> body) {
  if (condition == std::nullopt || body == std::nullopt)
    return Error{LazyString{L"Input missing."}};
  if (!condition.value()->IsBool()) {
    Error error{LazyString{L"Expected bool value for condition of \"while\" "
                           L"loop but found: "} +
                TypesToString(condition.value()->Types()) + LazyString{L"."}};
    compilation.AddError(error);
    return error;
  }

  return WhileExpression::New(std::move(condition)->ptr(),
                              std::move(body)->ptr());
}

ValueOrError<gc::Root<Expression>> NewForExpression(
    Compilation& compilation, std::optional<gc::Root<Expression>> init,
    std::optional<gc::Root<Expression>> condition,
    std::optional<gc::Root<Expression>> update,
    std::optional<gc::Root<Expression>> body) {
  if (init == std::nullopt || condition == std::nullopt ||
      update == std::nullopt || body == std::nullopt)
    return Error{LazyString{L"Input missing."}};
  ASSIGN_OR_RETURN(
      language::gc::Root<Expression> body_expression,
      NewAppendExpression(compilation, body.value(), update.value()));
  ASSIGN_OR_RETURN(gc::Root<Expression> while_expression,
                   NewWhileExpression(compilation, std::move(condition),
                                      std::move(body_expression)));
  return NewAppendExpression(compilation, std::move(init).value(),
                             std::move(while_expression));
}
}  // namespace afc::vm
