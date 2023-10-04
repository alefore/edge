#include "src/vm/function_call.h"

#include <glog/logging.h>

#include <unordered_set>

#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/compilation.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/filter_similar_names.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace afc::vm {
namespace {
using language::EmptyValue;
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;

namespace gc = language::gc;

using ::operator<<;

PossibleError CheckFunctionArguments(
    const Type& type,
    const std::vector<NonNull<std::shared_ptr<Expression>>>& args) {
  const types::Function* function_type = std::get_if<types::Function>(&type);
  if (function_type == nullptr) {
    return Error(L"Expected function but found: `" + ToString(type) + L"`.");
  }

  if (function_type->inputs.size() != args.size()) {
    return Error(L"Invalid number of arguments: Expected " +
                 std::to_wstring(function_type->inputs.size()) +
                 L" but found " + std::to_wstring(args.size()));
  }

  for (size_t argument = 0; argument < args.size(); argument++) {
    if (!args[argument]->SupportsType(function_type->inputs[argument])) {
      return Error(
          L"Type mismatch in argument " + std::to_wstring(argument) +
          L": Expected " +
          TypesToString(std::vector({function_type->inputs[argument]})) +
          L" but found " + TypesToString(args[argument]->Types()));
    }
  }

  return Success();
}

std::vector<Type> DeduceTypes(
    Expression& func,
    const std::vector<NonNull<std::shared_ptr<Expression>>>& args) {
  std::unordered_set<Type> output;
  for (auto& type : func.Types()) {
    if (std::holds_alternative<EmptyValue>(
            CheckFunctionArguments(type, args))) {
      output.insert(std::get<types::Function>(type).output.get());
    }
  }
  return std::vector<Type>(output.begin(), output.end());
}

class FunctionCall : public Expression {
 public:
  FunctionCall(
      NonNull<std::shared_ptr<Expression>> func,
      NonNull<
          std::shared_ptr<std::vector<NonNull<std::shared_ptr<Expression>>>>>
          args)
      : func_(std::move(func)),
        args_(std::move(args)),
        types_(DeduceTypes(func_.value(), args_.value())) {}

  std::vector<Type> Types() override { return types_; }

  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    PurityType output = func_->purity();
    for (const NonNull<std::shared_ptr<Expression>>& a : args_.value()) {
      output = CombinePurityType(a->purity(), output);
      if (output == PurityType::kUnknown) return output;  // Optimization.
    }
    for (const auto& callback_type : func_->Types()) {
      output = CombinePurityType(
          std::get<types::Function>(callback_type).function_purity, output);
      if (output == PurityType::kUnknown) return output;  // Optimization.
    }
    return output;
  }

  futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
      Trampoline& trampoline, const Type& type) override {
    DVLOG(3) << "Function call evaluation starts.";
    std::vector<Type> type_inputs;
    for (auto& arg : args_.value()) {
      type_inputs.push_back(arg->Types()[0]);
    }

    return trampoline
        .Bounce(func_, types::Function{.output = type,
                                       .inputs = std::move(type_inputs),
                                       .function_purity = purity()})
        .Transform(
            [&trampoline, args_types = args_](EvaluationOutput callback) {
              if (callback.type == EvaluationOutput::OutputType::kReturn)
                return futures::Past(Success(std::move(callback)));
              DVLOG(6) << "Got function: " << callback.value.ptr().value();
              CHECK(std::holds_alternative<types::Function>(
                  callback.value.ptr()->type));
              futures::Future<ValueOrError<EvaluationOutput>> output;
              CaptureArgs(trampoline, std::move(output.consumer), args_types,
                          MakeNonNullShared<std::vector<gc::Root<Value>>>(),
                          callback.value.ptr()->LockCallback());
              return std::move(output.value);
            });
  }

 private:
  static void CaptureArgs(
      Trampoline& trampoline,
      futures::ValueOrError<EvaluationOutput>::Consumer consumer,
      NonNull<
          std::shared_ptr<std::vector<NonNull<std::shared_ptr<Expression>>>>>
          args_types,
      NonNull<std::shared_ptr<std::vector<gc::Root<Value>>>> values,
      Value::Callback callback) {
    DVLOG(5) << "Evaluating function parameters, args: " << values->size()
             << " of " << args_types->size();
    if (values->size() == args_types->size()) {
      DVLOG(4) << "No more parameters, performing function call.";
      callback(std::move(values.value()), trampoline)
          .SetConsumer(VisitCallback(overload{
              [consumer](Error error) {
                DVLOG(3) << "Function call aborted: " << error;
                consumer(std::move(error));
              },
              [consumer](gc::Root<Value> return_value) {
                DVLOG(5) << "Function call consumer gets value: "
                         << return_value.ptr().value();
                consumer(
                    Success(EvaluationOutput::New(std::move(return_value))));
              }}));
      return;
    }
    NonNull<std::shared_ptr<Expression>>& arg = args_types->at(values->size());
    DVLOG(6) << "Bounce with types: " << TypesToString(arg->Types());
    trampoline.Bounce(arg, arg->Types()[0])
        .SetConsumer(VisitCallback(
            overload{[consumer](Error error) { consumer(std::move(error)); },
                     [&trampoline, consumer, args_types, values,
                      callback](EvaluationOutput value) {
                       DVLOG(7) << "Got evaluation output.";
                       switch (value.type) {
                         case EvaluationOutput::OutputType::kReturn:
                           DVLOG(5) << "Received return value.";
                           return consumer(std::move(value));
                         case EvaluationOutput::OutputType::kContinue:
                           DVLOG(5) << "Received results of parameter "
                                    << values->size() + 1 << " (of "
                                    << args_types->size()
                                    << "): " << value.value.ptr().value();
                           values->push_back(std::move(value.value));
                           DVLOG(6) << "Recursive call.";
                           CaptureArgs(trampoline, consumer, args_types, values,
                                       callback);
                       }
                     }}));
  }

  // Expression that evaluates to get the function to call.
  const NonNull<std::shared_ptr<Expression>> func_;
  const NonNull<
      std::shared_ptr<std::vector<NonNull<std::shared_ptr<Expression>>>>>
      args_;
  const std::vector<Type> types_;
};

}  // namespace

NonNull<std::unique_ptr<Expression>> NewFunctionCall(
    NonNull<std::unique_ptr<Expression>> func,
    std::vector<NonNull<std::shared_ptr<Expression>>> args) {
  return MakeNonNullUnique<FunctionCall>(
      std::move(func),
      MakeNonNullShared<std::vector<NonNull<std::shared_ptr<Expression>>>>(
          std::move(args)));
}

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation, NonNull<std::unique_ptr<Expression>> func,
    std::vector<NonNull<std::shared_ptr<Expression>>> args) {
  std::vector<Error> errors;
  for (auto& type : func->Types()) {
    PossibleError check_results = CheckFunctionArguments(type, args);
    if (Error* error = std::get_if<Error>(&check_results); error != nullptr)
      errors.push_back(*error);
    else
      return NewFunctionCall(std::move(func), std::move(args)).get_unique();
  }

  CHECK(!errors.empty());
  compilation->AddError(MergeErrors(errors, L", "));
  return nullptr;
}

std::unique_ptr<Expression> NewMethodLookup(
    Compilation* compilation, std::unique_ptr<Expression> object_ptr,
    wstring method_name) {
  return VisitPointer(
      std::move(object_ptr),
      [&compilation, &method_name](NonNull<std::unique_ptr<Expression>> object)
          -> std::unique_ptr<Expression> {
        std::vector<Error> errors;
        // TODO: Better support polymorphism: don't return early assuming one of
        // the types of `object`.
        for (const auto& type : object->Types()) {
          types::ObjectName object_type_name = NameForType(type);

          const ObjectType* object_type =
              compilation->environment.ptr()->LookupObjectType(
                  object_type_name);

          if (object_type == nullptr) {
            errors.push_back(
                Error(L"Unknown type: \"" + ToString(type) + L"\""));
            continue;
          }

          std::vector<NonNull<Value*>> fields =
              object_type->LookupField(method_name);
          for (auto& field : fields) {
            CHECK_GE(std::get<types::Function>(field->type).inputs.size(), 1ul);
            CHECK(std::get<types::Function>(field->type).inputs[0] == type);
          }
          if (fields.empty()) {
            std::vector<std::wstring> alternatives;
            object_type->ForEachField(
                [&](const std::wstring& name, const Value&) {
                  alternatives.push_back(name);
                });
            std::vector<std::wstring> close_alternatives =
                FilterSimilarNames(method_name, std::move(alternatives));
            errors.push_back(Error(L"Unknown method: \"" +
                                   object_type->ToString() + L"::" +
                                   method_name + L"\"" +
                                   (close_alternatives.empty()
                                        ? L""
                                        : (L" (did you mean \"" +
                                           close_alternatives[0] + L"\"?)"))));
            continue;
          }

          // When evaluated, evaluates first `obj_expr` and then returns a
          // callback that wraps `delegates`, inserting the value that
          // `obj_expr` evaluated to and calling the right delegate (depending
          // on the desired type).
          class BindObjectExpression : public Expression {
           public:
            BindObjectExpression(NonNull<std::shared_ptr<Expression>> obj_expr,
                                 std::vector<NonNull<Value*>> delegates)
                : delegates_(std::move(delegates)),
                  external_types_([&]() {
                    NonNull<std::shared_ptr<std::vector<Type>>> output;
                    for (const NonNull<Value*>& delegate : delegates_)
                      output->push_back(
                          RemoveObjectFirstArgument(delegate->type));
                    return output;
                  }()),
                  obj_expr_(std::move(obj_expr)) {}

            std::vector<Type> Types() override {
              return external_types_.value();
            }

            std::unordered_set<Type> ReturnTypes() const override { return {}; }

            PurityType purity() override {
              PurityType output = obj_expr_->purity();
              for (const Type& delegate_type : external_types_.value())
                output = CombinePurityType(
                    output,
                    std::get<types::Function>(delegate_type).function_purity);
              return output;
            }

            futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
                Trampoline& trampoline, const Type& type) override {
              return trampoline.Bounce(obj_expr_, obj_expr_->Types()[0])
                  .Transform(
                      [type, external_types = external_types_,
                       delegates = delegates_,
                       &pool = trampoline.pool()](EvaluationOutput output)
                          -> ValueOrError<EvaluationOutput> {
                        switch (output.type) {
                          case EvaluationOutput::OutputType::kReturn:
                            return Success(std::move(output));
                          case EvaluationOutput::OutputType::kContinue:
                            for (auto& delegate : delegates)
                              if (GetImplicitPromotion(
                                      RemoveObjectFirstArgument(delegate->type),
                                      type) != nullptr) {
                                const types::Function& function_type =
                                    std::get<types::Function>(type);
                                return Success(
                                    EvaluationOutput::New(Value::NewFunction(
                                        pool, function_type.function_purity,
                                        function_type.output.get(),
                                        function_type.inputs,
                                        [obj = std::move(output.value),
                                         callback = delegate->LockCallback()](
                                            std::vector<gc::Root<Value>> args,
                                            Trampoline& trampoline_inner) {
                                          args.emplace(args.begin(), obj);
                                          return callback(std::move(args),
                                                          trampoline_inner);
                                        })));
                              }
                            LOG(FATAL)
                                << "Unable to find proper delegate with type: "
                                << type << ", candidates: "
                                << TypesToString(external_types.value());
                        }
                        language::Error error(L"Unhandled OutputType case.");
                        LOG(FATAL) << error;
                        return error;
                      });
            }

           private:
            static Type RemoveObjectFirstArgument(Type input) {
              types::Function input_function = std::get<types::Function>(input);
              CHECK(!input_function.inputs.empty());
              input_function.inputs.erase(input_function.inputs.begin());
              return input_function;
            }

            const std::vector<NonNull<Value*>> delegates_;

            // The actual types that the expression can deliver. Basically, a
            // function receiving the arguments that will be dispatched to a
            // delegate (after inserting the result from evaluating
            // `obj_expr_`).
            const NonNull<std::shared_ptr<std::vector<Type>>> external_types_;
            const NonNull<std::shared_ptr<Expression>> obj_expr_;
          };

          return std::make_unique<BindObjectExpression>(std::move(object),
                                                        fields);
        }

        CHECK(!errors.empty());
        compilation->AddError(MergeErrors(errors, L", "));
        return nullptr;
      },
      [] { return nullptr; });
}

futures::ValueOrError<gc::Root<Value>> Call(
    gc::Pool& pool, const Value& func, std::vector<gc::Root<Value>> args,
    std::function<void(std::function<void()>)> yield_callback) {
  CHECK(std::holds_alternative<types::Function>(func.type));
  std::vector<NonNull<std::shared_ptr<Expression>>> args_expr;
  for (auto& a : args) args_expr.push_back(NewConstantExpression(std::move(a)));
  return Evaluate(
      NewFunctionCall(
          NewConstantExpression(pool.NewRoot(MakeNonNullUnique<Value>(func))),
          std::move(args_expr)),
      pool, pool.NewRoot(MakeNonNullUnique<Environment>()), yield_callback);
}

}  // namespace afc::vm
