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
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;

struct Instance {
  std::shared_ptr<Environment> environment = std::make_shared<Environment>();
};

void StartClassDeclaration(Compilation* compilation, const std::wstring& name) {
  compilation->current_class.push_back(VMType::ObjectType(name));
  compilation->environment =
      std::make_shared<Environment>(std::move(compilation->environment));
}

namespace {
NonNull<std::unique_ptr<Value>> BuildSetter(VMType class_type,
                                            VMType field_type,
                                            std::wstring field_name) {
  auto output = Value::NewFunction(
      {class_type, class_type, field_type},
      [field_name, field_type](
          std::vector<NonNull<std::unique_ptr<Value>>> args, Trampoline&) {
        CHECK_EQ(args.size(), 2u);
        auto instance = static_cast<Instance*>(args[0]->user_value.get());
        CHECK(instance != nullptr);

        CHECK_EQ(args[1]->type, field_type);
        instance->environment->Assign(field_name, std::move(args[1]));

        return futures::Past(
            Success(EvaluationOutput::New(std::move(args[0]))));
      });

  output->type.function_purity = Expression::PurityType::kUnknown;
  return output;
}

NonNull<std::unique_ptr<Value>> BuildGetter(VMType class_type,
                                            VMType field_type,
                                            std::wstring field_name) {
  auto output = Value::NewFunction(
      {field_type, class_type},
      [field_name, field_type](
          std::vector<NonNull<std::unique_ptr<Value>>> args, Trampoline&) {
        CHECK_EQ(args.size(), 1u);
        auto instance = static_cast<Instance*>(args[0]->user_value.get());
        CHECK(instance != nullptr);
        static Environment::Namespace empty_namespace;
        return futures::Past(VisitPointer(
            instance->environment->Lookup(empty_namespace, field_name,
                                          field_type),
            [](NonNull<std::unique_ptr<Value>> value) {
              return Success(EvaluationOutput::New(std::move(value)));
            },
            [&]() {
              return Error(L"Unexpected: variable value is null: " +
                           field_name);
            }));
      });
  output->type.function_purity = Expression::PurityType::kPure;
  return output;
}
}  // namespace

void FinishClassDeclaration(
    Compilation* compilation,
    NonNull<std::unique_ptr<Expression>> constructor_expression_input) {
  CHECK(compilation != nullptr);
  ValueOrError<NonNull<std::unique_ptr<Expression>>> constructor_expression =
      NewAppendExpression(std::move(constructor_expression_input),
                          NewVoidExpression());
  if (constructor_expression.IsError()) {
    compilation->errors.push_back(constructor_expression.error().description);
    return;
  }
  auto class_type = std::move(compilation->current_class.back());
  compilation->current_class.pop_back();
  auto class_object_type = MakeNonNullUnique<ObjectType>(class_type);

  auto class_environment = compilation->environment;
  compilation->environment = compilation->environment->parent_environment();

  std::map<std::wstring, Value> values;
  class_environment->ForEachNonRecursive(
      [&values, &class_object_type, class_type](std::wstring name,
                                                Value& value) {
        class_object_type->AddField(name,
                                    BuildGetter(class_type, value.type, name));
        class_object_type->AddField(L"set_" + name,
                                    BuildSetter(class_type, value.type, name));
      });
  compilation->environment->DefineType(class_type.object_type,
                                       std::move(class_object_type));
  auto purity = constructor_expression.value()->purity();
  std::unique_ptr<Value> constructor = Value::NewFunction(
      {class_type},
      [constructor_expression_shared = NonNull<std::shared_ptr<Expression>>(
           std::move(constructor_expression.value())),
       class_environment, class_type,
       values](std::vector<NonNull<std::unique_ptr<Value>>>,
               Trampoline& trampoline) {
        auto instance_environment = std::make_shared<Environment>(
            class_environment->parent_environment());
        auto original_environment = trampoline.environment();
        trampoline.SetEnvironment(instance_environment);
        return trampoline.Bounce(*constructor_expression_shared, VMType::Void())
            .Transform([constructor_expression_shared, original_environment,
                        class_type, instance_environment,
                        &trampoline](EvaluationOutput constructor_evaluation)
                           -> language::ValueOrError<EvaluationOutput> {
              trampoline.SetEnvironment(original_environment);
              switch (constructor_evaluation.type) {
                case EvaluationOutput::OutputType::kReturn:
                  return Error(
                      L"Unexpected: return (inside class declaration).");
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
      });
  constructor->type.function_purity = purity;

  compilation->environment->Define(class_type.object_type,
                                   std::move(constructor));
}

}  // namespace afc::vm
