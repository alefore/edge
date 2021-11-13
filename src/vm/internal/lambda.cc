#include "lambda.h"

#include <glog/logging.h>

#include "../public/constant_expression.h"
#include "../public/environment.h"
#include "../public/value.h"
#include "compilation.h"
#include "types_promotion.h"

namespace afc::vm {
namespace {
class LambdaExpression : public Expression {
 public:
  static std::unique_ptr<LambdaExpression> New(
      VMType type, std::shared_ptr<std::vector<std::wstring>> argument_names,
      std::shared_ptr<Expression> body, std::wstring* error) {
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
    std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>
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

  LambdaExpression(VMType type,
                   std::shared_ptr<std::vector<std::wstring>> argument_names,
                   std::shared_ptr<Expression> body,
                   std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>
                       promotion_function)
      : type_(std::move(type)),
        argument_names_(std::move(argument_names)),
        body_(std::move(body)),
        promotion_function_(std::move(promotion_function)) {
    CHECK(body_ != nullptr);
    CHECK_EQ(type_.type, VMType::FUNCTION);
    CHECK(type_.function_purity == body_->purity());
  }

  std::vector<VMType> Types() { return {type_}; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::Value<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                            const VMType& type) {
    auto promotion_function = GetImplicitPromotion(type_, type);
    CHECK(promotion_function != nullptr);
    return futures::Past(EvaluationOutput::New(
        promotion_function(BuildValue(trampoline->environment()))));
  }

  std::unique_ptr<Value> BuildValue(
      std::shared_ptr<Environment> parent_environment) {
    CHECK(parent_environment != nullptr);
    auto output = std::make_unique<Value>(VMType::FUNCTION);
    output->type = type_;
    output->callback =
        [body = body_, parent_environment, argument_names = argument_names_,
         promotion_function = promotion_function_](
            vector<unique_ptr<Value>> args, Trampoline* trampoline) {
          CHECK_EQ(args.size(), argument_names->size())
              << "Invalid number of arguments for function.";
          auto environment = std::make_shared<Environment>(parent_environment);
          for (size_t i = 0; i < args.size(); i++) {
            environment->Define(argument_names->at(i), std::move(args.at(i)));
          }
          auto original_trampoline = *trampoline;
          trampoline->SetEnvironment(environment);
          return trampoline->Bounce(body.get(), body->Types()[0])
              .Transform([original_trampoline, trampoline, body,
                          promotion_function](EvaluationOutput body_output) {
                *trampoline = original_trampoline;
                body_output.type = EvaluationOutput::OutputType::kContinue;
                body_output.value =
                    promotion_function(std::move(body_output.value));
                return body_output;
              });
        };
    return output;
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<LambdaExpression>(type_, argument_names_, body_,
                                              promotion_function_);
  }

 private:
  VMType type_;
  const std::shared_ptr<std::vector<std::wstring>> argument_names_;
  const std::shared_ptr<Expression> body_;
  const std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>
      promotion_function_;
};
}  // namespace

std::unique_ptr<UserFunction> UserFunction::New(
    Compilation* compilation, std::wstring return_type,
    std::optional<std::wstring> name,
    std::vector<std::pair<VMType, wstring>>* args) {
  if (args == nullptr) {
    return nullptr;
  }
  const VMType* return_type_def =
      compilation->environment->LookupType(return_type);
  if (return_type_def == nullptr) {
    compilation->errors.push_back(L"Unknown return type: \"" + return_type +
                                  L"\"");
    return nullptr;
  }

  auto output = std::make_unique<UserFunction>();
  output->type.type = VMType::FUNCTION;
  output->type.type_arguments.push_back(*return_type_def);
  for (pair<VMType, wstring> arg : *args) {
    output->type.type_arguments.push_back(arg.first);
    output->argument_names->push_back(arg.second);
  }
  if (name.has_value()) {
    output->name = name.value();
    compilation->environment->Define(name.value(),
                                     std::make_unique<Value>(output->type));
  }
  compilation->environment =
      std::make_shared<Environment>(compilation->environment);
  for (pair<VMType, wstring> arg : *args) {
    compilation->environment->Define(arg.second,
                                     std::make_unique<Value>(arg.first));
  }
  return output;
}

std::unique_ptr<Value> UserFunction::BuildValue(
    Compilation* compilation, std::unique_ptr<Expression> body,
    std::wstring* error) {
  std::shared_ptr<Environment> environment = compilation->environment;
  compilation->environment = compilation->environment->parent_environment();
  auto expression = LambdaExpression::New(
      std::move(type), std::move(argument_names), std::move(body), error);
  return expression == nullptr ? nullptr
                               : expression->BuildValue(std::move(environment));
}

std::unique_ptr<Expression> UserFunction::BuildExpression(
    Compilation* compilation, std::unique_ptr<Expression> body,
    std::wstring* error) {
  // We ignore the environment used during the compilation. Instead, each time
  // the expression is evaluated, it will use the environment from the
  // trampoline, correctly receiving the actual values in that environment.
  compilation->environment = compilation->environment->parent_environment();

  return LambdaExpression::New(std::move(type), std::move(argument_names),
                               std::move(body), error);
}

void UserFunction::Abort(Compilation* compilation) {
  compilation->environment = compilation->environment->parent_environment();
  if (name.has_value()) {
    compilation->environment->Remove(name.value(), type);
  }
}

}  // namespace afc::vm
