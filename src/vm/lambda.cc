#include "src/vm/lambda.h"

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {
class LambdaExpression : public Expression {
 public:
  static ValueOrError<NonNull<std::unique_ptr<LambdaExpression>>> New(
      Type lambda_type,
      NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body) {
    auto& lambda_function_type = std::get<types::Function>(lambda_type);
    lambda_function_type.function_purity = body->purity();
    Type expected_return_type = lambda_function_type.output.get();
    auto deduced_types = body->ReturnTypes();
    if (deduced_types.empty()) {
      deduced_types.insert(types::Void{});
    }
    if (deduced_types.size() > 1) {
      return Error{LazyString{L"Found multiple return types: "} +
                   TypesToString(deduced_types)};
    }
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
        promotion_function =
            GetImplicitPromotion(*deduced_types.begin(), expected_return_type);
    if (promotion_function == nullptr) {
      return Error{LazyString{L"Expected a return type of "} +
                   ToQuotedSingleLine(expected_return_type) +
                   LazyString{L" but found "} +
                   ToQuotedSingleLine(*deduced_types.cbegin()) +
                   LazyString{L"."}};
    }
    return MakeNonNullUnique<LambdaExpression>(
        std::move(lambda_type), std::move(argument_names), std::move(body),
        std::move(promotion_function));
  }

  LambdaExpression(
      Type type,
      NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body,
      std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
          promotion_function)
      : type_(std::move(type)),
        argument_names_(std::move(argument_names)),
        body_(std::move(body)),
        promotion_function_(std::move(promotion_function)) {
    CHECK(std::get<types::Function>(type_).function_purity == body_->purity());
  }

  std::vector<Type> Types() override { return {type_}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
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
          gc::Root<Environment> environment =
              Environment::New(parent_environment);
          for (size_t i = 0; i < args.size(); i++) {
            environment.ptr()->Define(argument_names->at(i),
                                      std::move(args.at(i)));
          }
          auto original_trampoline = trampoline;
          trampoline.SetEnvironment(environment);
          return trampoline.Bounce(body, body->Types()[0])
              .Transform(
                  [original_trampoline, &trampoline, body,
                   promotion_function](EvaluationOutput body_output) mutable {
                    trampoline = std::move(original_trampoline);
                    return Success(promotion_function(
                        trampoline.pool(), std::move(body_output.value)));
                  });
        },
        [parent_environment] {
          return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
              {parent_environment.object_metadata()});
        });
  }

 private:
  Type type_;
  const NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names_;
  const NonNull<std::shared_ptr<Expression>> body_;
  const std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
      promotion_function_;
};
}  // namespace

std::unique_ptr<UserFunction> UserFunction::New(
    Compilation& compilation, Identifier return_type,
    std::optional<Identifier> name,
    std::unique_ptr<std::vector<std::pair<Type, Identifier>>> args) {
  if (args == nullptr) return nullptr;
  const Type* return_type_def =
      compilation.environment.ptr()->LookupType(return_type);
  if (return_type_def == nullptr) {
    compilation.AddError(Error{LazyString{L"Unknown return type: \""} +
                               return_type.read().read() + LazyString{L"\""}});
    return nullptr;
  }

  types::Function function_type{
      .output = *return_type_def,
      .inputs = container::MaterializeVector(
          *args |
          std::views::transform(
              [](const std::pair<Type, Identifier>& a) { return a.first; }))};

  auto output = std::make_unique<UserFunction>(UserFunction{
      .name = std::nullopt,
      .type = std::move(function_type),
      .argument_names = MakeNonNullShared<std::vector<Identifier>>(
          container::MaterializeVector(
              *args |
              std::views::transform([](const std::pair<Type, Identifier>& a) {
                return a.second;
              })))});

  if (name.has_value()) {
    output->name = name.value();
    compilation.environment.ptr()->Define(
        name.value(), Value::New(compilation.pool, output->type));
  }
  compilation.environment = Environment::New(compilation.environment.ptr());
  for (const std::pair<Type, Identifier>& arg : *args)
    compilation.environment.ptr()->Define(
        arg.second, Value::New(compilation.pool, arg.first));
  return output;
}

gc::Root<Environment> GetOrCreateParentEnvironment(Compilation& compilation) {
  if (std::optional<gc::Ptr<Environment>> parent_environment =
          compilation.environment.ptr()->parent_environment();
      parent_environment.has_value())
    return parent_environment->ToRoot();
  return Environment::New(compilation.pool);
}

ValueOrError<gc::Root<Value>> UserFunction::BuildValue(
    Compilation& compilation, NonNull<std::unique_ptr<Expression>> body) {
  DECLARE_OR_RETURN(
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
  DECLARE_OR_RETURN(
      NonNull<std::unique_ptr<LambdaExpression>> expression,
      LambdaExpression::New(std::move(type), std::move(argument_names),
                            std::move(body)));
  return expression;
}

void UserFunction::Abort(Compilation& compilation) {
  Done(compilation);
  if (name.has_value())
    compilation.environment.ptr()->Remove(name.value(), type);
}

void UserFunction::Done(Compilation& compilation) {
  compilation.environment = GetOrCreateParentEnvironment(compilation);
}
}  // namespace afc::vm
