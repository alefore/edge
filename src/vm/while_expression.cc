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
    return trampoline.Bounce(condition.ptr(), types::Bool{})
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
              return trampoline.Bounce(body.ptr(), body->Types()[0])
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
    Compilation& compilation,
    ValueOrError<gc::Ptr<Expression>> condition_or_error,
    ValueOrError<gc::Ptr<Expression>> body_or_error) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> condition, condition_or_error)
  DECLARE_OR_RETURN(gc::Ptr<Expression> body, body_or_error)
  if (!condition->IsBool())
    return compilation.AddError(
        Error{LazyString{L"Expected bool value for condition of \"while\" "
                         L"loop but found: "} +
              TypesToString(condition->Types()) + LazyString{L"."}});

  return WhileExpression::New(std::move(condition), std::move(body));
}

ValueOrError<gc::Root<Expression>> NewForExpression(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> init,
    ValueOrError<gc::Ptr<Expression>> condition,
    ValueOrError<gc::Ptr<Expression>> update,
    ValueOrError<gc::Ptr<Expression>> body) {
  return NewAppendExpression(
      compilation, std::move(init),
      ToPtr(NewWhileExpression(
          compilation, std::move(condition),
          ToPtr(NewAppendExpression(compilation, body, update)))));
}
}  // namespace afc::vm
