#include "src/vm/class_expression.h"

#include <glog/logging.h>

#include "src/language/gc.h"
#include "src/language/overload.h"
#include "src/vm/append_expression.h"
#include "src/vm/compilation.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::vm {
using afc::language::lazy_string::ToLazyString;

struct Instance {
  static gc::Root<Environment> Read(const Type& class_type,
                                    const gc::Root<Value>& obj) {
    return obj.ptr()->get_user_value<Instance>(class_type)->environment;
  }

  gc::Root<Environment> environment;
};

void StartClassDeclaration(Compilation& compilation,
                           const types::ObjectName& name) {
  compilation.current_class.push_back(name);
  compilation.environment = Environment::New(compilation.environment).ptr();
}

namespace {
gc::Root<Value> BuildSetter(gc::Pool& pool, Type class_type, Type field_type,
                            Identifier field_name) {
  return Value::NewFunction(
      pool, kPurityTypeUnknown, class_type, {class_type, field_type},
      [class_type, field_name, field_type](std::vector<gc::Root<Value>> args,
                                           Trampoline&) {
        CHECK_EQ(args.size(), 2u);
        CHECK(args[1].ptr()->type() == field_type);
        Instance::Read(class_type, args[0])
            .ptr()
            ->Assign(field_name, std::move(args[1]));

        return futures::Past(Success(std::move(args[0])));
      });
}

gc::Root<Value> BuildGetter(gc::Pool& pool, Type class_type, Type field_type,
                            Identifier field_name) {
  return Value::NewFunction(
      pool, PurityType{}, field_type, {class_type},
      [class_type, field_name, field_type](std::vector<gc::Root<Value>> args,
                                           Trampoline&) {
        CHECK_EQ(args.size(), 1u);
        gc::Root<vm::Environment> environment =
            Instance::Read(class_type, args[0]);
        static const vm::Namespace empty_namespace;
        return futures::Past(VisitOptional(
            [](Environment::LookupResult value)
                -> ValueOrError<gc::Root<Value>> {
              return Success(std::get<gc::Root<Value>>(value.value));
            },
            [&] -> ValueOrError<gc::Root<vm::Value>> {
              return Error{
                  LazyString{L"Unexpected: variable value is null: "} +
                  QuoteExpr(language::lazy_string::ToSingleLine(field_name))};
            },
            environment->Lookup(empty_namespace, field_name, field_type)));
      });
}
}  // namespace

PossibleError FinishClassDeclaration(
    Compilation& compilation,
    gc::Root<Expression> constructor_expression_input) {
  gc::Pool& pool = compilation.pool;
  ASSIGN_OR_RETURN(gc::Root<Expression> constructor_expression,
                   compilation.RegisterErrors(NewAppendExpression(
                       compilation, constructor_expression_input.ptr(),
                       NewVoidExpression(compilation.pool).ptr())));
  auto class_type = std::move(compilation.current_class.back());
  compilation.current_class.pop_back();
  gc::Root<ObjectType> class_object_type = ObjectType::New(pool, class_type);

  gc::Root<Environment> class_environment = compilation.environment.ToRoot();
  // This is safe because StartClassDeclaration creates a sub-environment.
  CHECK(class_environment->parent_environment().has_value());
  compilation.environment = class_environment->parent_environment().value();

  class_environment.ptr()->ForEachNonRecursive(
      [&class_object_type, class_type, &pool](
          Identifier name, const std::variant<UninitializedValue,
                                              gc::Ptr<Value>>& value_optional) {
        std::visit(
            overload{
                [&](const gc::Ptr<Value>& value) {
                  class_object_type.ptr()->AddField(
                      name,
                      BuildGetter(pool, class_type, value->type(), name).ptr());
                  class_object_type.ptr()->AddField(
                      Identifier(SingleLine{LazyString{L"set_"}} + name.read()),
                      BuildSetter(pool, class_type, value->type(), name).ptr());
                },
                [](UninitializedValue) {}},
            value_optional);
      });
  compilation.environment->DefineType(class_object_type.ptr());
  compilation.environment->Define(
      Identifier{NonEmptySingleLine{
          SingleLine{ToLazyString(std::get<types::ObjectName>(class_type))}}},
      Value::NewFunction(
          pool, constructor_expression->purity(), class_type, {},
          [&pool, constructor_expression, class_environment, class_type](
              std::vector<gc::Root<Value>>, Trampoline& trampoline) {
            gc::Root<Environment> instance_environment = VisitOptional(
                [](gc::Ptr<vm::Environment> parent) {
                  return Environment::New(std::move(parent));
                },
                [&pool] { return Environment::New(pool); },
                class_environment.ptr()->parent_environment());
            auto original_environment = trampoline.environment();
            trampoline.SetEnvironment(instance_environment.ptr());
            return trampoline
                .Bounce(constructor_expression.ptr(), types::Void{})
                .Transform([constructor_expression, original_environment,
                            class_type, instance_environment, &trampoline](
                               EvaluationOutput constructor_evaluation)
                               -> language::ValueOrError<gc::Root<Value>> {
                  trampoline.SetEnvironment(original_environment);
                  switch (constructor_evaluation.type) {
                    case EvaluationOutput::OutputType::kReturn:
                      return Error{LazyString{
                          L"Unexpected: return (inside class declaration)."}};
                    case EvaluationOutput::OutputType::kContinue:
                      return Success(Value::NewObject(
                          trampoline.pool(),
                          std::get<types::ObjectName>(class_type),
                          MakeNonNullShared<Instance>(
                              Instance{.environment = instance_environment})));
                  }
                  language::Error error{
                      LazyString{L"Unhandled OutputType case."}};
                  LOG(FATAL) << error;
                  return error;
                });
          }));
  return Success();
}

}  // namespace afc::vm
