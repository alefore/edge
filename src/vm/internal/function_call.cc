#include "../public/function_call.h"

#include <glog/logging.h>

#include <unordered_set>

#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/internal/filter_similar_names.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

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

PossibleError CheckFunctionArguments(
    const VMType& type,
    const std::vector<NonNull<std::unique_ptr<Expression>>>& args) {
  const types::Function* function_type =
      std::get_if<types::Function>(&type.variant);
  if (function_type == nullptr) {
    return Error(L"Expected function but found: `" + type.ToString() + L"`.");
  }

  if (function_type->type_arguments.size() != args.size() + 1) {
    return Error(L"Invalid number of arguments: Expected " +
                 std::to_wstring(function_type->type_arguments.size() - 1) +
                 L" but found " + std::to_wstring(args.size()));
  }

  for (size_t argument = 0; argument < args.size(); argument++) {
    if (!args[argument]->SupportsType(
            function_type->type_arguments[1 + argument])) {
      return Error(L"Type mismatch in argument " + std::to_wstring(argument) +
                   L": Expected `" +
                   function_type->type_arguments[1 + argument].ToString() +
                   L"` but found " + TypesToString(args[argument]->Types()));
    }
  }

  return Success();
}

std::vector<VMType> DeduceTypes(
    Expression& func,
    const std::vector<NonNull<std::unique_ptr<Expression>>>& args) {
  std::unordered_set<VMType> output;
  for (auto& type : func.Types()) {
    if (std::holds_alternative<EmptyValue>(
            CheckFunctionArguments(type, args))) {
      output.insert(std::get<types::Function>(type.variant).type_arguments[0]);
    }
  }
  return std::vector<VMType>(output.begin(), output.end());
}

class FunctionCall : public Expression {
 public:
  FunctionCall(
      NonNull<std::shared_ptr<Expression>> func,
      NonNull<
          std::shared_ptr<std::vector<NonNull<std::unique_ptr<Expression>>>>>
          args)
      : func_(std::move(func)),
        args_(std::move(args)),
        types_(DeduceTypes(func_.value(), args_.value())) {}

  std::vector<VMType> Types() override { return types_; }

  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    PurityType output = func_->purity();
    for (const NonNull<std::unique_ptr<Expression>>& a : args_.value()) {
      output = CombinePurityType(a->purity(), output);
      if (output == PurityType::kUnknown) return output;  // Optimization.
    }
    for (const auto& callback_type : func_->Types()) {
      output = CombinePurityType(
          std::get<types::Function>(callback_type.variant).function_purity,
          output);
      if (output == PurityType::kUnknown) return output;  // Optimization.
    }
    return output;
  }

  futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
      Trampoline& trampoline, const VMType& type) {
    DVLOG(3) << "Function call evaluation starts.";
    std::vector<VMType> type_arguments = {type};
    for (auto& arg : args_.value()) {
      type_arguments.push_back(arg->Types()[0]);
    }

    return trampoline
        .Bounce(func_.value(),
                VMType{.variant = types::Function{.type_arguments =
                                                      std::move(type_arguments),
                                                  .function_purity = purity()}})
        .Transform(
            [&trampoline, args_types = args_](EvaluationOutput callback) {
              if (callback.type == EvaluationOutput::OutputType::kReturn)
                return futures::Past(Success(std::move(callback)));
              DVLOG(6) << "Got function: " << callback.value.ptr().value();
              CHECK(std::holds_alternative<types::Function>(
                  callback.value.ptr()->type.variant));
              futures::Future<ValueOrError<EvaluationOutput>> output;
              CaptureArgs(trampoline, std::move(output.consumer), args_types,
                          MakeNonNullShared<std::vector<gc::Root<Value>>>(),
                          callback.value.ptr()->LockCallback());
              return std::move(output.value);
            });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<FunctionCall>(func_, args_);
  }

 private:
  static void CaptureArgs(
      Trampoline& trampoline,
      futures::ValueOrError<EvaluationOutput>::Consumer consumer,
      NonNull<
          std::shared_ptr<std::vector<NonNull<std::unique_ptr<Expression>>>>>
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
              [consumer](EvaluationOutput return_value) {
                DVLOG(5) << "Function call consumer gets value: "
                         << return_value.value.ptr().value();
                consumer(Success(
                    EvaluationOutput::New(std::move(return_value.value))));
              }}));
      return;
    }
    NonNull<std::unique_ptr<Expression>>& arg = args_types->at(values->size());
    trampoline.Bounce(arg.value(), arg->Types()[0])
        .SetConsumer(VisitCallback(
            overload{[consumer](Error error) { consumer(std::move(error)); },
                     [&trampoline, consumer, args_types, values,
                      callback](EvaluationOutput value) {
                       switch (value.type) {
                         case EvaluationOutput::OutputType::kReturn:
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
      std::shared_ptr<std::vector<NonNull<std::unique_ptr<Expression>>>>>
      args_;
  const std::vector<VMType> types_;
};

}  // namespace

NonNull<std::unique_ptr<Expression>> NewFunctionCall(
    NonNull<std::unique_ptr<Expression>> func,
    std::vector<NonNull<std::unique_ptr<Expression>>> args) {
  return MakeNonNullUnique<FunctionCall>(
      std::move(func),
      MakeNonNullShared<std::vector<NonNull<std::unique_ptr<Expression>>>>(
          std::move(args)));
}

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation, NonNull<std::unique_ptr<Expression>> func,
    std::vector<NonNull<std::unique_ptr<Expression>>> args) {
  std::vector<Error> errors;
  for (auto& type : func->Types()) {
    PossibleError check_results = CheckFunctionArguments(type, args);
    if (Error* error = std::get_if<Error>(&check_results); error != nullptr)
      errors.push_back(*error);
    else
      return std::move(
          NewFunctionCall(std::move(func), std::move(args)).get_unique());
  }

  CHECK(!errors.empty());
  compilation->AddError(MergeErrors(errors, L", "));
  return nullptr;
}

std::unique_ptr<Expression> NewMethodLookup(Compilation* compilation,
                                            std::unique_ptr<Expression> object,
                                            wstring method_name) {
  return VisitPointer(
      std::move(object),
      [&compilation, &method_name](NonNull<std::unique_ptr<Expression>> object)
          -> std::unique_ptr<Expression> {
        // TODO: Support polymorphism.
        std::vector<Error> errors;
        for (const auto& type : object->Types()) {
          VMTypeObjectTypeName object_type_name = NameForType(type.variant);

          const ObjectType* object_type =
              compilation->environment.ptr()->LookupObjectType(
                  object_type_name);

          if (object_type == nullptr) {
            errors.push_back(
                Error(L"Unknown type: \"" + type.ToString() + L"\""));
            continue;
          }

          auto field = object_type->LookupField(method_name);
          if (field == nullptr) {
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
          // callback that wraps `delegate`, inserting the value that `obj_expr`
          // evaluated to.
          class BindObjectExpression : public Expression {
           public:
            BindObjectExpression(NonNull<std::shared_ptr<Expression>> obj_expr,
                                 Value* delegate)
                : type_([=]() {
                    auto output = std::make_shared<VMType>(delegate->type);
                    auto& output_function_type =
                        std::get<types::Function>(output->variant);
                    output_function_type.type_arguments.erase(
                        output_function_type.type_arguments.begin() + 1);
                    return output;
                  }()),
                  obj_expr_(std::move(obj_expr)),
                  delegate_(delegate) {}

            std::vector<VMType> Types() override { return {*type_}; }
            std::unordered_set<VMType> ReturnTypes() const override {
              return {};
            }

            NonNull<std::unique_ptr<Expression>> Clone() override {
              return MakeNonNullUnique<BindObjectExpression>(obj_expr_,
                                                             delegate_);
            }

            PurityType purity() override {
              return CombinePurityType(
                  obj_expr_->purity(),
                  delegate_function_type()->function_purity);
            }

            futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
                Trampoline& trampoline, const VMType& type) override {
              return trampoline.Bounce(obj_expr_.value(), obj_expr_->Types()[0])
                  .Transform([type, shared_type = type_,
                              callback = delegate_->LockCallback(),
                              purity_type =
                                  delegate_function_type()->function_purity,
                              &pool =
                                  trampoline.pool()](EvaluationOutput output)
                                 -> ValueOrError<EvaluationOutput> {
                    switch (output.type) {
                      case EvaluationOutput::OutputType::kReturn:
                        return Success(std::move(output));
                      case EvaluationOutput::OutputType::kContinue:
                        return Success(EvaluationOutput::New(Value::NewFunction(
                            pool, purity_type,
                            std::get<types::Function>(shared_type->variant)
                                .type_arguments,
                            [obj = std::move(output.value), callback](
                                std::vector<gc::Root<Value>> args,
                                Trampoline& trampoline) {
                              args.emplace(args.begin(), obj);
                              return callback(std::move(args), trampoline);
                            })));
                    }
                    language::Error error(L"Unhandled OutputType case.");
                    LOG(FATAL) << error;
                    return error;
                  });
            }

           private:
            types::Function* delegate_function_type() {
              return &std::get<types::Function>(delegate_->type.variant);
            }
            const std::shared_ptr<VMType> type_;
            const NonNull<std::shared_ptr<Expression>> obj_expr_;
            Value* const delegate_;
          };

          CHECK_GE(std::get<types::Function>(field->type.variant)
                       .type_arguments.size(),
                   2ul);
          CHECK_EQ(
              std::get<types::Function>(field->type.variant).type_arguments[1],
              type);

          return std::make_unique<BindObjectExpression>(std::move(object),
                                                        field);
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
  CHECK(std::holds_alternative<types::Function>(func.type.variant));
  std::vector<NonNull<std::unique_ptr<Expression>>> args_expr;
  for (auto& a : args) {
    args_expr.push_back(NewConstantExpression(std::move(a)));
  }
  NonNull<std::unique_ptr<Expression>> expr = NewFunctionCall(
      NewConstantExpression(pool.NewRoot(MakeNonNullUnique<Value>(func))),
      std::move(args_expr));
  return Evaluate(expr.value(), pool,
                  pool.NewRoot(MakeNonNullUnique<Environment>()),
                  yield_callback);
}

}  // namespace afc::vm
