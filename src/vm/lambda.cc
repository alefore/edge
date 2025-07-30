#include "src/vm/lambda.h"

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_view.h"
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
  struct ConstructorAccessTag {};

 public:
  static language::gc::Root<LambdaExpression> New(
      language::gc::Pool& pool,
      Type type,
      NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body,
      ImplicitPromotionCallback promotion_function) {
    return pool.NewRoot(language::MakeNonNullUnique<LambdaExpression>(type, argument_names, body, promotion_function));
  }

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
    ImplicitPromotionCallback promotion_function =
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
      ImplicitPromotionCallback promotion_function)
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
          auto original_trampoline = trampoline;
          trampoline.stack().Push(
              StackFrame::New(trampoline.pool(), container::MaterializeVector(
                                                     args | gc::view::Ptr))
                  .ptr());
          gc::Root<Environment> environment =
              Environment::New(parent_environment);
          for (size_t i = 0; i < args.size(); i++) {
            environment.ptr()->Define(argument_names->at(i),
                                      std::move(args.at(i)));
          }
          trampoline.SetEnvironment(environment);
          return trampoline.Bounce(body, body->Types()[0])
              .Transform(
                  [original_trampoline, &trampoline, body,
                   promotion_function](EvaluationOutput body_output) mutable {
                    gc::Root<Value> promoted_value =
                        promotion_function(std::move(body_output.value));
                    trampoline.stack().Pop();
                    trampoline = std::move(original_trampoline);
                    return Success(promoted_value);
                  });
        },
        [parent_environment] {
          return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
              {parent_environment.object_metadata()});
        });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const override {
    return {};
  }

 private:
  Type type_;
  const NonNull<std::shared_ptr<std::vector<Identifier>>> argument_names_;
  const NonNull<std::shared_ptr<Expression>> body_;
  const ImplicitPromotionCallback promotion_function_;
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

  return std::make_unique<UserFunction>(compilation, name,
                                        std::move(function_type), *args);
}

UserFunction::UserFunction(Compilation& compilation,
                           std::optional<Identifier> name, Type type,
                           std::vector<std::pair<Type, Identifier>> args)
    : compilation_(compilation),
      name_(std::move(name)),
      type_(type),
      argument_names_(MakeNonNullShared<std::vector<Identifier>>(
          container::MaterializeVector(
              args |
              std::views::transform([](const std::pair<Type, Identifier>& a) {
                return a.second;
              })))) {
  if (name_.has_value())
    compilation_.environment->DefineUninitialized(name_.value(), type_);
  compilation_.environment = Environment::New(compilation_.environment.ptr());
  for (const std::pair<Type, Identifier>& arg : args)
    compilation_.environment->DefineUninitialized(arg.second, arg.first);
  compilation.PushStackFrameHeader(
      StackFrameHeader{container::MaterializeVector(
          args |
          std::views::transform([](const std::pair<Type, Identifier>& data) {
            return std::pair{data.second, data.first};
          }))});
}

gc::Root<Environment> GetOrCreateParentEnvironment(Compilation& compilation) {
  if (std::optional<gc::Ptr<Environment>> parent_environment =
          compilation.environment.ptr()->parent_environment();
      parent_environment.has_value())
    return parent_environment->ToRoot();
  return Environment::New(compilation.pool);
}

ValueOrError<gc::Root<Value>> UserFunction::BuildValue(
    NonNull<std::unique_ptr<Expression>> body) {
  DECLARE_OR_RETURN(
      NonNull<std::unique_ptr<LambdaExpression>> expression,
      LambdaExpression::New(type_, argument_names_, std::move(body)));
  return expression->BuildValue(compilation_.pool, compilation_.environment);
}

ValueOrError<NonNull<std::unique_ptr<Expression>>>
UserFunction::BuildExpression(NonNull<std::unique_ptr<Expression>> body) {
  // We can't just return the result of LambdaExpression::New; that's a
  // ValueOrError<LambdaExpression>. We need to explicitly convert it to a
  // ValueOrError<Expression>.
  DECLARE_OR_RETURN(
      NonNull<std::unique_ptr<LambdaExpression>> expression,
      LambdaExpression::New(type_, argument_names_, std::move(body)));
  return expression;
}

void UserFunction::Abort() {
  if (name_.has_value())
    compilation_.environment.ptr()->Remove(name_.value(), type_);
}

const std::optional<Identifier>& UserFunction::name() const { return name_; }
const Type& UserFunction::type() const { return type_; }

UserFunction::~UserFunction() {
  compilation_.environment = GetOrCreateParentEnvironment(compilation_);
  compilation_.PopStackFrameHeader();
}
}  // namespace afc::vm
