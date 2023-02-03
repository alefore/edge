#include "src/vm/internal/lambda.h"

#include <glog/logging.h>

#include "src/language/value_or_error.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/internal/types_promotion.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;

namespace gc = language::gc;

class LambdaExpression : public Expression {
 public:
  static ValueOrError<NonNull<std::unique_ptr<LambdaExpression>>> New(
      Type lambda_type,
      NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body) {
    auto& lambda_function_type = std::get<types::Function>(lambda_type);
    lambda_function_type.function_purity = body->purity();
    Type expected_return_type = lambda_function_type.output.get();
    auto deduced_types = body->ReturnTypes();
    if (deduced_types.empty()) {
      deduced_types.insert(types::Void{});
    }
    if (deduced_types.size() > 1) {
      return Error(L"Found multiple return types: " +
                   TypesToString(deduced_types));
    }
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
        promotion_function =
            GetImplicitPromotion(*deduced_types.begin(), expected_return_type);
    if (promotion_function == nullptr) {
      return Error(L"Expected a return type of `" +
                   ToString(expected_return_type) + L"` but found `" +
                   ToString(*deduced_types.cbegin()) + L"`.");
    }
    return MakeNonNullUnique<LambdaExpression>(
        std::move(lambda_type), std::move(argument_names), std::move(body),
        std::move(promotion_function));
  }

  LambdaExpression(
      Type type,
      NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body,
      std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
          promotion_function)
      : type_(std::move(type)),
        argument_names_(std::move(argument_names)),
        body_(std::move(body)),
        promotion_function_(std::move(promotion_function)) {
    CHECK(std::get<types::Function>(type_).function_purity == body_->purity());
  }

  std::vector<Type> Types() { return {type_}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) {
    auto promotion_function = GetImplicitPromotion(type_, type);
    CHECK(promotion_function != nullptr);
    return futures::Past(Success(EvaluationOutput::New(promotion_function(
        trampoline.pool(),
        BuildValue(trampoline.pool(), trampoline.environment())))));
  }

  gc::Root<Value> BuildValue(gc::Pool& pool,
                             gc::Root<Environment> parent_environment_root) {
    gc::Ptr<Environment> parent_environment = parent_environment_root.ptr();
    const types::Function& function_type = std::get<types::Function>(type_);
    return Value::NewFunction(
        pool, body_->purity(), function_type.output.get(), function_type.inputs,
        [body = body_, parent_environment, argument_names = argument_names_,
         promotion_function = promotion_function_](
            std::vector<gc::Root<Value>> args, Trampoline& trampoline) {
          CHECK_EQ(args.size(), argument_names->size())
              << "Invalid number of arguments for function.";
          gc::Root<Environment> environment = trampoline.pool().NewRoot(
              MakeNonNullUnique<Environment>(parent_environment));
          for (size_t i = 0; i < args.size(); i++) {
            environment.ptr()->Define(argument_names->at(i),
                                      std::move(args.at(i)));
          }
          auto original_trampoline = trampoline;
          trampoline.SetEnvironment(environment);
          return trampoline.Bounce(body.value(), body->Types()[0])
              .Transform([original_trampoline, &trampoline, body,
                          promotion_function](EvaluationOutput body_output) {
                trampoline = original_trampoline;
                return Success(EvaluationOutput::New(promotion_function(
                    trampoline.pool(), std::move(body_output.value))));
              });
        },
        [parent_environment] {
          return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
              {parent_environment.object_metadata()});
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<LambdaExpression>(type_, argument_names_, body_,
                                               promotion_function_);
  }

 private:
  Type type_;
  const NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names_;
  const NonNull<std::shared_ptr<Expression>> body_;
  const std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
      promotion_function_;
};
}  // namespace

std::unique_ptr<UserFunction> UserFunction::New(
    Compilation& compilation, std::wstring return_type,
    std::optional<std::wstring> name,
    std::vector<std::pair<Type, wstring>>* args) {
  if (args == nullptr) {
    return nullptr;
  }
  const Type* return_type_def =
      compilation.environment.ptr()->LookupType(return_type);
  if (return_type_def == nullptr) {
    compilation.AddError(
        Error(L"Unknown return type: \"" + return_type + L"\""));
    return nullptr;
  }

  types::Function function_type{.output = *return_type_def};
  for (pair<Type, wstring> arg : *args) {
    function_type.inputs.push_back(arg.first);
  }

  auto output = std::make_unique<UserFunction>(
      UserFunction{.name = std::nullopt,
                   .type = std::move(function_type),
                   .argument_names = {}});
  for (pair<Type, wstring> arg : *args) {
    output->argument_names->push_back(arg.second);
  }

  if (name.has_value()) {
    output->name = name.value();
    compilation.environment.ptr()->Define(
        name.value(), Value::New(compilation.pool, output->type));
  }
  compilation.environment = compilation.pool.NewRoot(
      MakeNonNullUnique<Environment>(compilation.environment.ptr()));
  for (const pair<Type, wstring>& arg : *args) {
    compilation.environment.ptr()->Define(
        arg.second, Value::New(compilation.pool, arg.first));
  }
  return output;
}

gc::Root<Environment> GetOrCreateParentEnvironment(Compilation& compilation) {
  if (std::optional<gc::Ptr<Environment>> parent_environment =
          compilation.environment.ptr()->parent_environment();
      parent_environment.has_value())
    return parent_environment->ToRoot();
  return compilation.pool.NewRoot(MakeNonNullUnique<Environment>());
}

ValueOrError<gc::Root<Value>> UserFunction::BuildValue(
    Compilation& compilation, NonNull<std::unique_ptr<Expression>> body) {
  ASSIGN_OR_RETURN(
      NonNull<std::unique_ptr<LambdaExpression>> expression,
      LambdaExpression::New(std::move(type), std::move(argument_names),
                            std::move(body)));
  gc::Root<Environment> environment = compilation.environment;
  compilation.environment = GetOrCreateParentEnvironment(compilation);
  return expression->BuildValue(compilation.pool, std::move(environment));
}

ValueOrError<NonNull<std::unique_ptr<Expression>>>
UserFunction::BuildExpression(Compilation& compilation,
                              NonNull<std::unique_ptr<Expression>> body) {
  // We ignore the environment used during the compilation. Instead, each time
  // the expression is evaluated, it will use the environment from the
  // trampoline, correctly receiving the actual values in that environment.
  compilation.environment = GetOrCreateParentEnvironment(compilation);
  // We can't just return the result of LambdaExpression::New; that's a
  // ValueOrError<LambdaExpression>. We need to explicitly convert it to a
  // ValueOrError<Expression>.
  ASSIGN_OR_RETURN(
      NonNull<std::unique_ptr<LambdaExpression>> expression,
      LambdaExpression::New(std::move(type), std::move(argument_names),
                            std::move(body)));
  return expression;
}

void UserFunction::Abort(Compilation& compilation) {
  Done(compilation);
  if (name.has_value()) {
    compilation.environment.ptr()->Remove(name.value(), type);
  }
}

void UserFunction::Done(Compilation& compilation) {
  compilation.environment = GetOrCreateParentEnvironment(compilation);
}
}  // namespace afc::vm
