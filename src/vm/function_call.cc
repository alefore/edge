#include "src/vm/function_call.h"

#include <glog/logging.h>

#include <unordered_set>

#include "src/language/container.h"
#include "src/language/error/view.h"
#include "src/language/gc_container.h"
#include "src/language/gc_view.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/compilation.h"
#include "src/vm/constant_expression.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/filter_similar_names.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::view::SkipErrors;

namespace afc::vm {
using afc::language::lazy_string::ToLazyString;

namespace {
using ::operator<<;

PossibleError CheckFunctionArguments(
    const Type& type, const std::vector<gc::Ptr<Expression>>& args) {
  const types::Function* function_type = std::get_if<types::Function>(&type);
  if (function_type == nullptr) {
    return Error{LazyString{L"Expected function but found: "} +
                 ToQuotedSingleLine(type) + LazyString{L"."}};
  }

  if (function_type->inputs.size() != args.size()) {
    return Error{LazyString{L"Invalid number of arguments: Expected "} +
                 LazyString{std::to_wstring(function_type->inputs.size())} +
                 LazyString{L" but found "} +
                 LazyString{std::to_wstring(args.size())}};
  }

  for (size_t argument = 0; argument < args.size(); argument++) {
    if (!args[argument]->SupportsType(function_type->inputs[argument])) {
      return Error{
          LazyString{L"Type mismatch in argument "} +
          LazyString{std::to_wstring(argument)} + LazyString{L": Expected "} +
          TypesToString(std::vector({function_type->inputs[argument]})) +
          LazyString{L" but found "} + TypesToString(args[argument]->Types())};
    }
  }

  return Success();
}

std::vector<Type> DeduceTypes(Expression& func,
                              const std::vector<gc::Ptr<Expression>>& args) {
  return container::MaterializeVector(container::MaterializeUnorderedSet(
      func.Types() |
      std::views::transform([&args](const Type& type) -> ValueOrError<Type> {
        RETURN_IF_ERROR(CheckFunctionArguments(type, args));
        return std::get<types::Function>(type).output.get();
      }) |
      SkipErrors));
}

class FunctionCall : public Expression {
  struct ConstructorAccessTag {};

  // Expression that evaluates to get the function to call.
  const gc::Ptr<Expression> func_;
  const gc::Ptr<std::vector<gc::Ptr<Expression>>> args_;
  const std::vector<Type> types_;

 public:
  static language::gc::Root<FunctionCall> New(
      gc::Ptr<Expression> func,
      gc::Ptr<std::vector<gc::Ptr<Expression>>> args) {
    gc::Pool& pool = func.pool();
    return pool.NewRoot(language::MakeNonNullUnique<FunctionCall>(
        ConstructorAccessTag{}, func, args));
  }

  FunctionCall(ConstructorAccessTag, gc::Ptr<Expression> func,
               gc::Ptr<std::vector<gc::Ptr<Expression>>> args)
      : func_(std::move(func)),
        args_(std::move(args)),
        types_(DeduceTypes(func_.value(), args_.value())) {}

  std::vector<Type> Types() override { return types_; }

  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    return CombinePurityType(container::MaterializeVector(
        std::vector<std::vector<PurityType>>{
            {func_->purity()},
            container::MaterializeVector(
                args_.value() |
                std::views::transform(
                    [](const gc::Ptr<Expression>& a) { return a->purity(); })),
            container::MaterializeVector(
                func_->Types() |
                std::views::transform([](const auto& callback_type) {
                  return std::get<types::Function>(callback_type)
                      .function_purity;
                }))} |
        std::views::join));
  }

  futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
      Trampoline& trampoline, const Type& type) override {
    DVLOG(3) << "Function call evaluation starts.";
    return trampoline
        .Bounce(func_, types::Function{.output = type,
                                       .inputs = container::MaterializeVector(
                                           args_.value() |
                                           std::views::transform([](auto& arg) {
                                             return arg->Types()[0];
                                           })),
                                       .function_purity = purity()})
        .Transform([&trampoline,
                    args_root = args_.ToRoot()](EvaluationOutput callback) {
          if (callback.type == EvaluationOutput::OutputType::kReturn)
            return futures::Past(Success(std::move(callback)));
          DVLOG(6) << "Got function: " << callback.value.ptr().value();
          DVLOG(6) << "Is function: " << callback.value->IsFunction();
          CHECK(callback.value.ptr()->IsFunction());

          return CaptureArgs(trampoline, args_root,
                             MakeNonNullShared<std::vector<gc::Root<Value>>>(),
                             callback.value);
        });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {func_.object_metadata(), args_.object_metadata()};
  }

 private:
  static futures::ValueOrError<EvaluationOutput> CaptureArgs(
      Trampoline& trampoline, gc::Root<std::vector<gc::Ptr<Expression>>> args,
      NonNull<std::shared_ptr<std::vector<gc::Root<Value>>>> values,
      gc::Root<vm::Value> callback) {
    DVLOG(5) << "Evaluating function parameters, args: " << values->size()
             << " of " << args->size();
    if (values->size() == args->size()) {
      DVLOG(4) << "No more parameters, performing function call: "
               << callback.ptr().value();
      trampoline.stack().Push(
          StackFrame::New(
              trampoline.pool(),
              container::MaterializeVector(values.value() | gc::view::Ptr))
              .ptr());
      return callback->RunFunction(std::move(values.value()), trampoline)
          .Transform([&trampoline](gc::Root<Value> return_value) {
            DVLOG(5) << "Function call consumer gets value: "
                     << return_value.ptr().value();
            trampoline.stack().Pop();
            return futures::Past(
                Success(EvaluationOutput::New(std::move(return_value))));
          });
    }
    const gc::Ptr<Expression>& arg = args->at(values->size());
    DVLOG(6) << "Bounce with types: " << TypesToString(arg->Types());
    return trampoline.Bounce(arg, arg->Types()[0])
        .Transform(
            [&trampoline, args, values, callback](EvaluationOutput value) {
              DVLOG(7) << "Got evaluation output.";
              switch (value.type) {
                case EvaluationOutput::OutputType::kReturn:
                  DVLOG(5) << "Received return value.";
                  return futures::Past(Success(std::move(value)));
                case EvaluationOutput::OutputType::kContinue:
                  DVLOG(5) << "Received results of parameter "
                           << values->size() + 1 << " (of " << args->size()
                           << "): " << value.value.ptr().value();
                  values->push_back(std::move(value.value));
                  DVLOG(6) << "Recursive call.";
                  return CaptureArgs(trampoline, args, values, callback);
              }
              Error error{LazyString{L"Unsupported value type."}};
              LOG(FATAL) << error;
              return futures::Past(ValueOrError<EvaluationOutput>(error));
            });
  }
};

}  // namespace

gc::Root<Expression> NewFunctionCall(gc::Ptr<Expression> func,
                                     std::vector<gc::Ptr<Expression>> args) {
  gc::Pool& pool = func.pool();
  return FunctionCall::New(
      std::move(func),
      pool.NewRoot(MakeNonNullUnique<std::vector<gc::Ptr<Expression>>>(
                       std::move(args)))
          .ptr());
}

ValueOrError<gc::Root<Expression>> NewFunctionCall(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> func_or_error,
    std::vector<gc::Ptr<Expression>> args) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> func, std::move(func_or_error));
  std::vector<Error> errors;
  for (auto& type : func->Types()) {
    PossibleError check_results = CheckFunctionArguments(type, args);
    if (Error* error = std::get_if<Error>(&check_results); error != nullptr)
      errors.push_back(*error);
    else
      return NewFunctionCall(std::move(func), std::move(args));
  }

  CHECK(!errors.empty());
  return compilation.AddError(MergeErrors(errors, L", "));
}

ValueOrError<gc::Root<Expression>> NewMethodLookup(
    Compilation& compilation, ValueOrError<gc::Ptr<Expression>> object_or_error,
    Identifier method_name) {
  DECLARE_OR_RETURN(gc::Ptr<Expression> object, std::move(object_or_error));
  std::vector<Error> errors;
  // TODO: Better support polymorphism: don't return early assuming one of
  // the types of `object`.
  for (const auto& type : object->Types()) {
    types::ObjectName object_type_name = NameForType(type);

    const ObjectType* object_type =
        compilation.environment->LookupObjectType(object_type_name);

    if (object_type == nullptr) {
      errors.push_back(Error{LazyString{L"Unknown type: "} +
                             ToQuotedSingleLine(type) + LazyString{L"."}});
      continue;
    }

    std::vector<gc::Root<Value>> fields = object_type->LookupField(method_name);
    for (auto& field : fields) {
      CHECK(field.ptr()->IsFunction());
      CHECK_GE(std::get<types::Function>(field.ptr()->type()).inputs.size(),
               1ul);
      CHECK(std::get<types::Function>(field.ptr()->type()).inputs[0] == type);
    }
    if (fields.empty()) {
      std::vector<Identifier> alternatives;
      object_type->ForEachField([&](const Identifier& name, const Value&) {
        alternatives.push_back(name);
      });
      std::vector<Identifier> close_alternatives =
          FilterSimilarNames(method_name, std::move(alternatives));
      errors.push_back(Error{
          LazyString{L"Unknown method: "} +
          QuoteExpr(ToSingleLine(*object_type) + SINGLE_LINE_CONSTANT(L"::") +
                    language::lazy_string::ToSingleLine(method_name)) +
          (close_alternatives.empty()
               ? LazyString{}
               : (LazyString{L" (did you mean "} +
                  QuoteExpr(language::lazy_string::ToSingleLine(
                      close_alternatives[0])) +
                  LazyString{L"?)"}))});
      continue;
    }

    // When evaluated, evaluates first `obj_expr` and then returns a
    // callback that wraps `delegates`, inserting the value that
    // `obj_expr` evaluated to and calling the right delegate (depending
    // on the desired type).
    class BindObjectExpression : public Expression {
      struct ConstructorAccessTag {};

     public:
      static language::gc::Root<BindObjectExpression> New(
          gc::Ptr<Expression> obj_expr,
          std::vector<gc::Root<Value>> delegates) {
        gc::Pool& pool = obj_expr.pool();
        return pool.NewRoot(language::MakeNonNullUnique<BindObjectExpression>(
            ConstructorAccessTag{}, obj_expr, delegates));
      }

      BindObjectExpression(ConstructorAccessTag, gc::Ptr<Expression> obj_expr,
                           const std::vector<gc::Root<Value>>& delegates)
          : delegates_(container::MaterializeVector(delegates | gc::view::Ptr)),
            external_types_(MakeNonNullShared<std::vector<Type>>(
                container::MaterializeVector(
                    delegates_ |
                    std::views::transform([](const gc::Ptr<Value>& delegate) {
                      return RemoveObjectFirstArgument(delegate->type());
                    })))),
            obj_expr_(std::move(obj_expr)) {}

      std::vector<Type> Types() override { return external_types_.value(); }

      std::unordered_set<Type> ReturnTypes() const override { return {}; }

      PurityType purity() override {
        return CombinePurityType(
            {obj_expr_->purity(),
             CombinePurityType(container::MaterializeVector(
                 external_types_.value() |
                 std::views::transform([](const Type& delegate_type) {
                   return std::get<types::Function>(delegate_type)
                       .function_purity;
                 })))});
      }

      futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
          Trampoline& trampoline, const Type& type) override {
        std::optional<gc::Root<Value>> delegate;
        for (const gc::Ptr<Value>& candidate : delegates_)
          if (GetImplicitPromotion(RemoveObjectFirstArgument(candidate->type()),
                                   type) != nullptr)
            delegate = candidate.ToRoot();
        LOG_IF(FATAL, !delegate.has_value())
            << "Unable to find proper delegate with type: " << type
            << ", candidates: " << TypesToString(external_types_.value());

        return trampoline.Bounce(obj_expr_, obj_expr_->Types()[0])
            .Transform([type, external_types = external_types_,
                        delegate = delegate.value(),
                        &pool = trampoline.pool()](EvaluationOutput output)
                           -> ValueOrError<EvaluationOutput> {
              switch (output.type) {
                case EvaluationOutput::OutputType::kReturn:
                  return Success(std::move(output));
                case EvaluationOutput::OutputType::kContinue:
                  const types::Function& function_type =
                      std::get<types::Function>(type);
                  return Success(EvaluationOutput::New(Value::NewFunction(
                      pool, function_type.function_purity,
                      function_type.output.get(), function_type.inputs,
                      [obj = std::move(output.value), callback = delegate](
                          std::vector<gc::Root<Value>> args,
                          Trampoline& trampoline_inner) {
                        args.emplace(args.begin(), obj);
                        return callback.ptr()->RunFunction(std::move(args),
                                                           trampoline_inner);
                      })));
              }
              language::Error error{LazyString{L"Unhandled OutputType case."}};
              LOG(FATAL) << error;
              return error;
            });
      }

      std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
          const override {
        std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output =
            container::MaterializeVector(delegates_ | gc::view::ObjectMetadata);
        output.push_back(obj_expr_.object_metadata());
        return output;
      }

     private:
      static Type RemoveObjectFirstArgument(Type input) {
        types::Function input_function = std::get<types::Function>(input);
        CHECK(!input_function.inputs.empty());
        input_function.inputs.erase(input_function.inputs.begin());
        return input_function;
      }

      const std::vector<gc::Ptr<Value>> delegates_;

      // The actual types that the expression can deliver. Basically, a
      // function receiving the arguments that will be dispatched to a
      // delegate (after inserting the result from evaluating
      // `obj_expr_`).
      const NonNull<std::shared_ptr<std::vector<Type>>> external_types_;
      const gc::Ptr<Expression> obj_expr_;
    };

    return BindObjectExpression::New(std::move(object), fields);
  }

  CHECK(!errors.empty());
  return compilation.AddError(MergeErrors(errors, L", "));
}

futures::ValueOrError<gc::Root<Value>> Call(
    gc::Pool& pool, const Value& func, std::vector<gc::Root<Value>> args,
    std::function<void(OnceOnlyFunction<void()>)> yield_callback) {
  CHECK(std::holds_alternative<types::Function>(func.type()));
  CHECK_EQ(std::get<types::Function>(func.type()).inputs.size(), args.size());
  std::vector<gc::Root<Expression>> args_expr =
      args | std::views::transform([](gc::Root<Value> value) {
        return NewConstantExpression(value.ptr());
      }) |
      std::ranges::to<std::vector>();
  return Evaluate(
      FunctionCall::New(
          NewConstantExpression(
              pool.NewRoot(MakeNonNullUnique<Value>(func)).ptr())
              .ptr(),
          pool
              .NewRoot(MakeNonNullUnique<std::vector<gc::Ptr<Expression>>>(
                  container::MaterializeVector(args_expr | gc::view::Ptr)))
              .ptr())
          .ptr(),
      Environment::New(pool).ptr(), yield_callback);
}

}  // namespace afc::vm
