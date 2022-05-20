#include "src/vm/internal/lambda.h"

#include <glog/logging.h>

#include "../public/constant_expression.h"
#include "../public/environment.h"
#include "../public/value.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/internal/types_promotion.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
namespace gc = language::gc;

class LambdaExpression : public Expression {
 public:
  static std::unique_ptr<LambdaExpression> New(
      VMType type,
      NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body, std::wstring* error) {
    type.function_purity = body->purity();
    VMType expected_return_type = *type.type_arguments.cbegin();
    auto deduced_types = body->ReturnTypes();
    if (deduced_types.empty()) {
      deduced_types.insert(VMType::Void());
    }
    if (deduced_types.size() > 1) {
      *error = L"Found multiple return types: ";
      std::wstring separator;
      for (const auto& type : deduced_types) {
        *error += separator + L"`" + type.ToString() + L"`";
        separator = L", ";
      }
      return nullptr;
    }
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
        promotion_function =
            GetImplicitPromotion(*deduced_types.begin(), expected_return_type);
    if (promotion_function == nullptr) {
      *error = L"Expected a return type of `" +
               expected_return_type.ToString() + L"` but found `" +
               deduced_types.cbegin()->ToString() + L"`.";
      return nullptr;
    }
    return std::make_unique<LambdaExpression>(
        std::move(type), std::move(argument_names), std::move(body),
        std::move(promotion_function));
  }

  LambdaExpression(
      VMType type,
      NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names,
      NonNull<std::shared_ptr<Expression>> body,
      std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
          promotion_function)
      : type_(std::move(type)),
        argument_names_(std::move(argument_names)),
        body_(std::move(body)),
        promotion_function_(std::move(promotion_function)) {
    CHECK(type_.type == VMType::Type::kFunction);
    CHECK(type_.function_purity == body_->purity());
  }

  std::vector<VMType> Types() { return {type_}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const VMType& type) {
    auto promotion_function = GetImplicitPromotion(type_, type);
    CHECK(promotion_function != nullptr);
    return futures::Past(Success(EvaluationOutput::New(promotion_function(
        trampoline.pool(),
        BuildValue(trampoline.pool(), trampoline.environment())))));
  }

  gc::Root<Value> BuildValue(gc::Pool& pool,
                             gc::Root<Environment> parent_environment_root) {
    gc::Ptr<Environment> parent_environment = parent_environment_root.ptr();
    return Value::NewFunction(
        pool, body_->purity(), type_.type_arguments,
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
          return std::vector<NonNull<std::shared_ptr<gc::ControlFrame>>>(
              {parent_environment.control_frame()});
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<LambdaExpression>(type_, argument_names_, body_,
                                               promotion_function_);
  }

 private:
  VMType type_;
  const NonNull<std::shared_ptr<std::vector<std::wstring>>> argument_names_;
  const NonNull<std::shared_ptr<Expression>> body_;
  const std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>
      promotion_function_;
};
}  // namespace

std::unique_ptr<UserFunction> UserFunction::New(
    Compilation& compilation, std::wstring return_type,
    std::optional<std::wstring> name,
    std::vector<std::pair<VMType, wstring>>* args) {
  if (args == nullptr) {
    return nullptr;
  }
  const VMType* return_type_def =
      compilation.environment.ptr()->LookupType(return_type);
  if (return_type_def == nullptr) {
    compilation.errors.push_back(L"Unknown return type: \"" + return_type +
                                 L"\"");
    return nullptr;
  }

  auto output = std::make_unique<UserFunction>();
  output->type.type = VMType::Type::kFunction;
  output->type.type_arguments.push_back(*return_type_def);
  for (pair<VMType, wstring> arg : *args) {
    output->type.type_arguments.push_back(arg.first);
    output->argument_names->push_back(arg.second);
  }
  if (name.has_value()) {
    output->name = name.value();
    compilation.environment.ptr()->Define(
        name.value(), Value::New(compilation.pool, output->type));
  }
  compilation.environment = compilation.pool.NewRoot(
      MakeNonNullUnique<Environment>(compilation.environment.ptr()));
  for (const pair<VMType, wstring>& arg : *args) {
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

std::optional<gc::Root<Value>> UserFunction::BuildValue(
    Compilation& compilation, NonNull<std::unique_ptr<Expression>> body,
    std::wstring* error) {
  std::unique_ptr<LambdaExpression> expression = LambdaExpression::New(
      std::move(type), std::move(argument_names), std::move(body), error);
  if (expression == nullptr) return std::nullopt;
  gc::Root<Environment> environment = compilation.environment;
  compilation.environment = GetOrCreateParentEnvironment(compilation);
  return expression->BuildValue(compilation.pool, std::move(environment));
}

std::unique_ptr<Expression> UserFunction::BuildExpression(
    Compilation& compilation, NonNull<std::unique_ptr<Expression>> body,
    std::wstring* error) {
  // We ignore the environment used during the compilation. Instead, each time
  // the expression is evaluated, it will use the environment from the
  // trampoline, correctly receiving the actual values in that environment.
  compilation.environment = GetOrCreateParentEnvironment(compilation);
  return LambdaExpression::New(std::move(type), std::move(argument_names),
                               std::move(body), error);
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
