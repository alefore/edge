#include "../public/function_call.h"

#include <glog/logging.h>

#include <unordered_set>

#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

bool TypeMatchesArguments(const VMType& type,
                          const std::vector<std::unique_ptr<Expression>>& args,
                          wstring* error) {
  wstring dummy_error;
  if (error == nullptr) {
    error = &dummy_error;
  }

  if (type.type != VMType::FUNCTION) {
    *error = L"Expected function but found: `" + type.ToString() + L"`.";
    return false;
  }

  if (type.type_arguments.size() != args.size() + 1) {
    *error = L"Invalid number of arguments: Expected " +
             std::to_wstring(type.type_arguments.size() - 1) + L" but found " +
             std::to_wstring(args.size());
    return false;
  }

  for (size_t argument = 0; argument < args.size(); argument++) {
    if (!args[argument]->SupportsType(type.type_arguments[1 + argument])) {
      *error = L"Type mismatch in argument " + std::to_wstring(argument) +
               L": Expected `" + type.type_arguments[1 + argument].ToString() +
               L"` but found " + TypesToString(args[argument]->Types());
      return false;
    }
  }

  return true;
}

std::vector<VMType> DeduceTypes(
    Expression* func, const std::vector<std::unique_ptr<Expression>>& args) {
  CHECK(func != nullptr);
  std::unordered_set<VMType> output;
  for (auto& type : func->Types()) {
    if (TypeMatchesArguments(type, args, nullptr)) {
      output.insert(type.type_arguments[0]);
    }
  }
  return std::vector<VMType>(output.begin(), output.end());
}

class FunctionCall : public Expression {
 public:
  FunctionCall(
      NonNull<std::shared_ptr<Expression>> func,
      NonNull<std::shared_ptr<std::vector<std::unique_ptr<Expression>>>> args)
      : func_(std::move(func)),
        args_(std::move(args)),
        types_(DeduceTypes(func_.get(), *args_)) {}

  std::vector<VMType> Types() override { return types_; }

  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override {
    if (func_->purity() != PurityType::kPure) {
      return PurityType::kUnknown;
    }
    for (const auto& a : *args_) {
      if (a->purity() != PurityType::kPure) {
        return PurityType::kUnknown;
      }
    }
    for (const auto& callback_type : func_->Types()) {
      CHECK_EQ(callback_type.type, VMType::FUNCTION);
      if (callback_type.function_purity == PurityType::kPure) {
        return PurityType::kPure;
      }
    }
    return PurityType::kUnknown;
  }

  futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
      Trampoline* trampoline, const VMType& type) {
    DVLOG(3) << "Function call evaluation starts.";
    std::vector<VMType> type_arguments = {type};
    for (auto& arg : *args_) {
      type_arguments.push_back(arg->Types()[0]);
    }

    return trampoline
        ->Bounce(*func_, VMType::Function(std::move(type_arguments), purity()))
        .Transform([trampoline, args_types = args_](EvaluationOutput callback) {
          if (callback.type == EvaluationOutput::OutputType::kReturn)
            return futures::Past(Success(std::move(callback)));
          DVLOG(6) << "Got function: " << *callback.value;
          CHECK_EQ(callback.value->type.type, VMType::FUNCTION);
          CHECK(callback.value->callback != nullptr);
          futures::Future<ValueOrError<EvaluationOutput>> output;
          CaptureArgs(
              trampoline, std::move(output.consumer), args_types,
              std::make_shared<std::vector<NonNull<std::unique_ptr<Value>>>>(),
              std::move(callback.value));
          return std::move(output.value);
        });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<FunctionCall>(func_, args_);
  }

 private:
  static void CaptureArgs(
      Trampoline* trampoline,
      futures::ValueOrError<EvaluationOutput>::Consumer consumer,
      // TODO(easy, 2022-04-25): args_types should contain NonNull.
      NonNull<std::shared_ptr<std::vector<std::unique_ptr<Expression>>>>
          args_types,
      std::shared_ptr<std::vector<NonNull<unique_ptr<Value>>>> values,
      NonNull<std::shared_ptr<Value>> callback) {
    CHECK(values != nullptr);
    CHECK_EQ(callback->type.type, VMType::FUNCTION);
    CHECK(callback->callback != nullptr);

    DVLOG(5) << "Evaluating function parameters, args: " << values->size()
             << " of " << args_types->size();
    if (values->size() == args_types->size()) {
      DVLOG(4) << "No more parameters, performing function call.";
      callback->callback(std::move(*values), trampoline)
          .SetConsumer([consumer,
                        callback](ValueOrError<EvaluationOutput> return_value) {
            if (return_value.IsError()) {
              DVLOG(3) << "Function call aborted: " << return_value.error();
              return consumer(std::move(return_value.error()));
            }
            NonNull<std::unique_ptr<Value>> result =
                std::move(return_value.value().value);
            DVLOG(5) << "Function call consumer gets value: " << *result;
            consumer(Success(EvaluationOutput::New(std::move(result))));
          });
      return;
    }
    std::unique_ptr<Expression>& arg = args_types->at(values->size());
    trampoline->Bounce(*arg, arg->Types()[0])
        .SetConsumer([trampoline, consumer, args_types, values,
                      callback](ValueOrError<EvaluationOutput> value) {
          CHECK(values != nullptr);
          if (value.IsError()) return consumer(std::move(value.error()));
          switch (value.value().type) {
            case EvaluationOutput::OutputType::kReturn:
              return consumer(std::move(value));
            case EvaluationOutput::OutputType::kContinue:
              DVLOG(5) << "Received results of parameter " << values->size() + 1
                       << " (of " << args_types->size()
                       << "): " << *value.value().value;
              values->push_back(std::move(value.value().value));
              DVLOG(6) << "Recursive call.";
              CaptureArgs(trampoline, consumer, args_types, values, callback);
          }
        });
  }

  // Expression that evaluates to get the function to call.
  const NonNull<std::shared_ptr<Expression>> func_;
  const NonNull<std::shared_ptr<std::vector<std::unique_ptr<Expression>>>>
      args_;
  const std::vector<VMType> types_;
};

}  // namespace

NonNull<std::unique_ptr<Expression>> NewFunctionCall(
    NonNull<std::unique_ptr<Expression>> func,
    std::vector<std::unique_ptr<Expression>> args) {
  return MakeNonNullUnique<FunctionCall>(
      std::move(func),
      MakeNonNullShared<std::vector<std::unique_ptr<Expression>>>(
          std::move(args)));
}

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation, NonNull<std::unique_ptr<Expression>> func,
    std::vector<std::unique_ptr<Expression>> args) {
  std::vector<wstring> errors;
  wstring errors_separator;
  for (auto& type : func->Types()) {
    wstring error;
    if (TypeMatchesArguments(type, args, &error)) {
      return std::move(
          NewFunctionCall(std::move(func), std::move(args)).get_unique());
    }
    errors.push_back(errors_separator + error);
    errors_separator = L", ";
  }

  CHECK(!errors.empty());
  compilation->errors.push_back(errors[0]);
  return nullptr;
}

std::unique_ptr<Expression> NewMethodLookup(Compilation* compilation,
                                            std::unique_ptr<Expression> object,
                                            wstring method_name) {
  // TODO: Support polymorphism.
  std::vector<wstring> errors;
  for (const auto& type : object->Types()) {
    wstring object_type_name;
    switch (type.type) {
      case VMType::VM_STRING:
        object_type_name = L"string";
        break;
      case VMType::VM_BOOLEAN:
        object_type_name = L"bool";
        break;
      case VMType::VM_DOUBLE:
        object_type_name = L"double";
        break;
      case VMType::VM_INTEGER:
        object_type_name = L"int";
        break;
      case VMType::OBJECT_TYPE:
        object_type_name = type.object_type;
        break;
      default:
        break;
    }
    if (object_type_name.empty()) {
      errors.push_back(L"Unable to find methods on primitive type: \"" +
                       type.ToString() + L"\"");
      continue;
    }

    const ObjectType* object_type =
        compilation->environment->LookupObjectType(object_type_name);

    if (object_type == nullptr) {
      errors.push_back(L"Unknown type: \"" + type.ToString() + L"\"");
      continue;
    }

    auto field = object_type->LookupField(method_name);
    if (field == nullptr) {
      errors.push_back(L"Unknown method: \"" + object_type->ToString() + L"::" +
                       method_name + L"\"");
      continue;
    }

    // When evaluated, evaluates first `obj_expr` and then returns a callback
    // that wraps `delegate`, inserting the value that `obj_expr` evaluated to.
    class BindObjectExpression : public Expression {
     public:
      // TODO(easy, 2022-04-25): Receive obj_xpr as NonNull.
      BindObjectExpression(std::unique_ptr<Expression> obj_expr,
                           Value* delegate)
          : type_([=]() {
              auto output = std::make_shared<VMType>(delegate->type);
              output->type_arguments.erase(output->type_arguments.begin() + 1);
              return output;
            }()),
            obj_expr_(std::move(obj_expr)),
            delegate_(delegate) {}

      std::vector<VMType> Types() override { return {*type_}; }
      std::unordered_set<VMType> ReturnTypes() const override { return {}; }

      NonNull<std::unique_ptr<Expression>> Clone() override {
        return MakeNonNullUnique<BindObjectExpression>(
            std::move(obj_expr_->Clone().get_unique()), delegate_);
      }

      PurityType purity() override {
        return (obj_expr_->purity() == PurityType::kPure &&
                delegate_->type.function_purity == PurityType::kPure)
                   ? PurityType::kPure
                   : PurityType::kUnknown;
      }

      futures::Value<ValueOrError<EvaluationOutput>> Evaluate(
          Trampoline* trampoline, const VMType& type) override {
        return trampoline->Bounce(*obj_expr_, obj_expr_->Types()[0])
            .Transform([type, shared_type = type_,
                        shared_delegate = delegate_](EvaluationOutput output)
                           -> ValueOrError<EvaluationOutput> {
              switch (output.type) {
                case EvaluationOutput::OutputType::kReturn:
                  return Success(std::move(output));
                case EvaluationOutput::OutputType::kContinue:
                  return Success(EvaluationOutput::New(Value::NewFunction(
                      shared_type->type_arguments,
                      [obj = NonNull<std::shared_ptr<Value>>(
                           std::move(output.value)),
                       shared_delegate](std::vector<NonNull<Value::Ptr>> args,
                                        Trampoline* trampoline) {
                        args.emplace(args.begin(), MakeNonNullUnique<Value>(
                                                       *obj.get_shared()));
                        return shared_delegate->callback(std::move(args),
                                                         trampoline);
                      })));
              }
              language::Error error(L"Unhandled OutputType case.");
              LOG(FATAL) << error;
              return error;
            });
      }

     private:
      const std::shared_ptr<VMType> type_;
      const std::shared_ptr<Expression> obj_expr_;
      Value* const delegate_;
    };

    CHECK(field->type.type == VMType::FUNCTION);
    CHECK_GE(field->type.type_arguments.size(), 2ul);
    CHECK_EQ(field->type.type_arguments[1], type);

    return std::make_unique<BindObjectExpression>(
        std::move(object->Clone().get_unique()), field);
  }

  CHECK(!errors.empty());
  compilation->errors.push_back(errors[0]);
  return nullptr;
}

futures::ValueOrError<NonNull<std::unique_ptr<Value>>> Call(
    const Value& func, std::vector<NonNull<Value::Ptr>> args,
    std::function<void(std::function<void()>)> yield_callback) {
  CHECK_EQ(func.type.type, VMType::FUNCTION);
  std::vector<std::unique_ptr<Expression>> args_expr;
  for (auto& a : args) {
    args_expr.push_back(
        std::move(NewConstantExpression(std::move(a)).get_unique()));
  }
  NonNull<std::unique_ptr<Expression>> expr =
      NewFunctionCall(NewConstantExpression(Value::NewFunction(
                          func.type.type_arguments, func.callback)),
                      std::move(args_expr));
  return Evaluate(*expr, nullptr, yield_callback);
}

}  // namespace afc::vm
