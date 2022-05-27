#include "src/vm/internal/class_expression.h"

#include <glog/logging.h>

#include "src/language/gc.h"
#include "src/vm/internal/append_expression.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

namespace afc::vm {
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
namespace gc = language::gc;

struct Instance {
  static gc::Root<Environment> Read(const VMType& class_type,
                                    const gc::Root<Value>& obj) {
    return obj.ptr()->get_user_value<Instance>(class_type)->environment;
  }

  gc::Root<Environment> environment;
};

void StartClassDeclaration(Compilation& compilation,
                           const VMTypeObjectTypeName& name) {
  compilation.current_class.push_back(VMType::ObjectType(name));
  compilation.environment = compilation.pool.NewRoot<Environment>(
      MakeNonNullUnique<Environment>(compilation.environment.ptr()));
}

namespace {
gc::Root<Value> BuildSetter(gc::Pool& pool, VMType class_type,
                            VMType field_type, std::wstring field_name) {
  gc::Root<Value> output = Value::NewFunction(
      pool, PurityType::kUnknown, {class_type, class_type, field_type},
      [class_type, field_name, field_type](std::vector<gc::Root<Value>> args,
                                           Trampoline&) {
        CHECK_EQ(args.size(), 2u);
        CHECK_EQ(args[1].ptr()->type, field_type);
        Instance::Read(class_type, args[0])
            .ptr()
            ->Assign(field_name, std::move(args[1]));

        return futures::Past(
            Success(EvaluationOutput::New(std::move(args[0]))));
      });

  output.ptr()->type.function_purity = PurityType::kUnknown;
  return output;
}

gc::Root<Value> BuildGetter(gc::Pool& pool, VMType class_type,
                            VMType field_type, std::wstring field_name) {
  gc::Root<Value> output = Value::NewFunction(
      pool, PurityType::kPure, {field_type, class_type},
      [&pool, class_type, field_name, field_type](
          std::vector<gc::Root<Value>> args, Trampoline&) {
        CHECK_EQ(args.size(), 1u);
        gc::Root<vm::Environment> environment =
            Instance::Read(class_type, args[0]);
        static const Environment::Namespace empty_namespace;
        return futures::Past(VisitPointer(
            environment.ptr()->Lookup(pool, empty_namespace, field_name,
                                      field_type),
            [](gc::Root<Value> value) {
              return Success(EvaluationOutput::New(std::move(value)));
            },
            [&]() {
              return Error(L"Unexpected: variable value is null: " +
                           field_name);
            }));
      });
  output.ptr()->type.function_purity = PurityType::kPure;
  return output;
}
}  // namespace

void FinishClassDeclaration(
    Compilation& compilation,
    NonNull<std::unique_ptr<Expression>> constructor_expression_input) {
  ValueOrError<NonNull<std::unique_ptr<Expression>>> constructor_expression =
      NewAppendExpression(std::move(constructor_expression_input),
                          NewVoidExpression(compilation.pool));
  if (constructor_expression.IsError()) {
    compilation.errors.push_back(constructor_expression.error().description);
    return;
  }
  auto class_type = std::move(compilation.current_class.back());
  compilation.current_class.pop_back();
  auto class_object_type = MakeNonNullUnique<ObjectType>(class_type);

  gc::Root<Environment> class_environment = compilation.environment;
  // This is safe because StartClassDeclaration creates a sub-environment.
  CHECK(class_environment.ptr()->parent_environment().has_value());
  compilation.environment =
      class_environment.ptr()->parent_environment()->ToRoot();

  gc::Pool& pool = compilation.pool;

  std::map<std::wstring, Value> values;
  class_environment.ptr()->ForEachNonRecursive(
      [&values, &class_object_type, class_type, &pool](
          std::wstring name, const gc::Ptr<Value>& value) {
        class_object_type->AddField(
            name, BuildGetter(pool, class_type, value->type, name));
        class_object_type->AddField(
            L"set_" + name, BuildSetter(pool, class_type, value->type, name));
      });
  compilation.environment.ptr()->DefineType(std::move(class_object_type));
  auto purity = constructor_expression.value()->purity();
  gc::Root<Value> constructor = Value::NewFunction(
      pool, PurityType::kPure, {class_type},
      [constructor_expression_shared = NonNull<std::shared_ptr<Expression>>(
           std::move(constructor_expression.value())),
       class_environment, class_type,
       values](std::vector<gc::Root<Value>>, Trampoline& trampoline) {
        gc::Root<Environment> instance_environment =
            trampoline.pool().NewRoot(MakeNonNullUnique<Environment>(
                class_environment.ptr()->parent_environment()));
        auto original_environment = trampoline.environment();
        trampoline.SetEnvironment(instance_environment);
        return trampoline
            .Bounce(constructor_expression_shared.value(), VMType::Void())
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
                      trampoline.pool(), class_type.object_type,
                      MakeNonNullShared<Instance>(
                          Instance{.environment = instance_environment}))));
              }
              language::Error error(L"Unhandled OutputType case.");
              LOG(FATAL) << error;
              return error;
            });
      });
  constructor.ptr()->type.function_purity = purity;

  compilation.environment.ptr()->Define(class_type.object_type.read(),
                                        std::move(constructor));
}

}  // namespace afc::vm
