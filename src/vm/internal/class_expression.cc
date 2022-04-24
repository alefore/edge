#include "class_expression.h"

#include <glog/logging.h>

#include "src/vm/internal/append_expression.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc::vm {
using language::Error;
using language::Success;
struct Instance {
  std::shared_ptr<Environment> environment = std::make_shared<Environment>();
};

void StartClassDeclaration(Compilation* compilation, const std::wstring& name) {
  compilation->current_class.push_back(VMType::ObjectType(name));
  compilation->environment =
      std::make_shared<Environment>(std::move(compilation->environment));
}

namespace {
std::unique_ptr<Value> BuildSetter(VMType class_type, VMType field_type,
                                   std::wstring field_name) {
  auto output = std::make_unique<Value>(VMType::FUNCTION);
  output->type.function_purity = Expression::PurityType::kUnknown;
  output->type.type_arguments = {class_type, class_type, field_type};
  output->callback = [field_name, field_type](
                         std::vector<std::unique_ptr<Value>> args,
                         Trampoline*) {
    CHECK_EQ(args.size(), 2u);
    auto instance = static_cast<Instance*>(args[0]->user_value.get());
    CHECK(instance != nullptr);

    CHECK_EQ(args[1]->type, field_type);
    instance->environment->Assign(field_name, std::move(args[1]));

    return futures::Past(Success(EvaluationOutput::New(std::move(args[0]))));
  };
  return output;
}

std::unique_ptr<Value> BuildGetter(VMType class_type, VMType field_type,
                                   std::wstring field_name) {
  auto output = std::make_unique<Value>(VMType::FUNCTION);
  output->type.function_purity = Expression::PurityType::kPure;
  output->type.type_arguments = {field_type, class_type};
  output->callback = [field_name, field_type](
                         std::vector<std::unique_ptr<Value>> args,
                         Trampoline*) {
    CHECK_EQ(args.size(), 1u);
    auto instance = static_cast<Instance*>(args[0]->user_value.get());
    CHECK(instance != nullptr);
    static Environment::Namespace empty_namespace;
    return futures::Past(
        Success(EvaluationOutput::New(instance->environment->Lookup(
            empty_namespace, field_name, field_type))));
  };
  return output;
}
}  // namespace

void FinishClassDeclaration(
    Compilation* compilation,
    std::unique_ptr<Expression> constructor_expression) {
  CHECK(compilation != nullptr);
  CHECK(constructor_expression != nullptr);
  std::shared_ptr<Expression> constructor_expression_shared =
      NewAppendExpression(compilation, std::move(constructor_expression),
                          NewVoidExpression());
  auto class_type = std::move(compilation->current_class.back());
  compilation->current_class.pop_back();
  auto class_object_type = std::make_unique<ObjectType>(class_type);

  auto class_environment = compilation->environment;
  compilation->environment = compilation->environment->parent_environment();

  std::map<std::wstring, Value> values;
  class_environment->ForEachNonRecursive(
      [&values, &class_object_type, class_type](std::wstring name,
                                                Value* value) {
        CHECK(value != nullptr);
        class_object_type->AddField(name,
                                    BuildGetter(class_type, value->type, name));
        class_object_type->AddField(L"set_" + name,
                                    BuildSetter(class_type, value->type, name));
      });
  compilation->environment->DefineType(class_type.object_type,
                                       std::move(class_object_type));
  auto constructor = std::make_unique<Value>(VMType::FUNCTION);
  constructor->type.function_purity = constructor_expression_shared->purity();
  constructor->type.type_arguments.push_back(class_type);
  constructor->callback = [constructor_expression_shared, class_environment,
                           class_type,
                           values](std::vector<std::unique_ptr<Value>>,
                                   Trampoline* trampoline) {
    auto instance_environment =
        std::make_shared<Environment>(class_environment->parent_environment());
    auto original_environment = trampoline->environment();
    trampoline->SetEnvironment(instance_environment);
    return trampoline
        ->Bounce(constructor_expression_shared.get(), VMType::Void())
        .Transform([constructor_expression_shared, original_environment,
                    class_type, instance_environment,
                    trampoline](EvaluationOutput constructor_evaluation)
                       -> language::ValueOrError<EvaluationOutput> {
          trampoline->SetEnvironment(original_environment);
          switch (constructor_evaluation.type) {
            case EvaluationOutput::OutputType::kReturn:
              return Error(L"Unexpected: return (inside class declaration).");
            case EvaluationOutput::OutputType::kContinue:
              return Success(EvaluationOutput::New(Value::NewObject(
                  class_type.object_type,
                  std::make_shared<Instance>(
                      Instance{.environment = instance_environment}))));
          }
          language::Error error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return error;
        });
  };
  compilation->environment->Define(class_type.object_type,
                                   std::move(constructor));
}

}  // namespace afc::vm
