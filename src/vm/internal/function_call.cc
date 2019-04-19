#include "../public/function_call.h"

#include <glog/logging.h>

#include <unordered_set>

#include "../public/constant_expression.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "src/vm/internal/compilation.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace vm {

namespace {
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
               L": Expected \"" + type.type_arguments[1 + argument].ToString() +
               L"\" but found \"" + TypesToString(args[argument]->Types()) +
               L"\"";
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
  FunctionCall(std::shared_ptr<Expression> func,
               std::shared_ptr<std::vector<std::unique_ptr<Expression>>> args)
      : func_(std::move(func)),
        args_(std::move(args)),
        types_(DeduceTypes(func_.get(), *args_)) {
    CHECK(func_ != nullptr);
    CHECK(args_ != nullptr);
  }

  std::vector<VMType> Types() override { return types_; }

  void Evaluate(Trampoline* trampoline, const VMType& type) {
    DVLOG(3) << "Function call evaluation starts.";
    auto args_types = args_;
    auto func = func_;

    std::vector<VMType> type_arguments = {type};
    for (auto& arg : *args_) {
      type_arguments.push_back(arg->Types()[0]);
    }

    trampoline->Bounce(func_.get(), VMType::Function(std::move(type_arguments)),
                       [func, args_types](std::unique_ptr<Value> callback,
                                          Trampoline* trampoline) {
                         DVLOG(6) << "Got function: " << *callback;
                         CaptureArgs(
                             trampoline, args_types,
                             std::make_shared<vector<unique_ptr<Value>>>(),
                             std::move(callback));
                       });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<FunctionCall>(func_, args_);
  }

 private:
  static void CaptureArgs(
      Trampoline* trampoline,
      std::shared_ptr<std::vector<std::unique_ptr<Expression>>> args_types,
      std::shared_ptr<std::vector<unique_ptr<Value>>> values,
      std::shared_ptr<Value> callback) {
    CHECK(args_types != nullptr);
    CHECK(values != nullptr);
    CHECK(callback != nullptr);
    CHECK_EQ(callback->type.type, VMType::FUNCTION);
    CHECK(callback->callback);

    DVLOG(5) << "Evaluating function parameters, args: " << args_types->size();
    if (values->size() == args_types->size()) {
      DVLOG(4) << "No more parameters, performing function call.";
      trampoline->SetReturnContinuation(
          [original_trampoline = *trampoline](std::unique_ptr<Value> value,
                                              Trampoline* trampoline) {
            CHECK(value != nullptr);
            DVLOG(3) << "Got returned value: " << *value;
            // We have to make a copy because assigning to *trampoline may
            // delete us (and thus deletes original_trampoline as it is being
            // read).
            Trampoline tmp_copy = original_trampoline;
            *trampoline = tmp_copy;
            trampoline->Continue(std::move(value));
          });
      trampoline->SetContinuation(trampoline->return_continuation());
      callback->callback(std::move(*values), trampoline);
      return;
    }
    auto arg = args_types->at(values->size()).get();
    trampoline->Bounce(
        arg, arg->Types()[0],
        [args_types, values, callback](std::unique_ptr<Value> value,
                                       Trampoline* trampoline) {
          CHECK(values != nullptr);
          DVLOG(5) << "Received results of parameter " << values->size() + 1
                   << " (of " << args_types->size() << "): " << *value;
          values->push_back(std::move(value));
          DVLOG(6) << "Recursive call.";
          CHECK(callback != nullptr);
          CHECK_EQ(callback->type.type, VMType::FUNCTION);
          CHECK(callback->callback);
          CaptureArgs(trampoline, args_types, values, callback);
        });
  }

  const std::shared_ptr<Expression> func_;
  const std::shared_ptr<std::vector<std::unique_ptr<Expression>>> args_;
  const std::vector<VMType> types_;
};

}  // namespace

std::unique_ptr<Expression> NewFunctionCall(
    std::unique_ptr<Expression> func,
    std::vector<std::unique_ptr<Expression>> args) {
  return std::make_unique<FunctionCall>(
      std::move(func),
      std::make_shared<std::vector<std::unique_ptr<Expression>>>(
          std::move(args)));
}

std::unique_ptr<Expression> NewFunctionCall(
    Compilation* compilation, std::unique_ptr<Expression> func,
    std::vector<std::unique_ptr<Expression>> args) {
  std::vector<wstring> errors;
  wstring errors_separator;
  for (auto& type : func->Types()) {
    wstring error;
    if (TypeMatchesArguments(type, args, &error)) {
      return NewFunctionCall(std::move(func), std::move(args));
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

      std::unique_ptr<Expression> Clone() override {
        return std::make_unique<BindObjectExpression>(obj_expr_->Clone(),
                                                      delegate_);
      }

      void Evaluate(Trampoline* evaluation, const VMType& type) override {
        evaluation->Bounce(
            obj_expr_.get(), obj_expr_->Types()[0],
            [type, shared_type = type_, shared_obj_expr = obj_expr_,
             shared_delegate = delegate_](Value::Ptr obj,
                                          Trampoline* trampoline) {
              trampoline->Continue(Value::NewFunction(
                  shared_type->type_arguments,
                  [obj = std::shared_ptr(std::move(obj)), shared_delegate](
                      std::vector<Value::Ptr> args, Trampoline* trampoline) {
                    args.emplace(args.begin(), std::make_unique<Value>(*obj));
                    shared_delegate->callback(std::move(args), trampoline);
                  }));
            });
      }

     private:
      const std::shared_ptr<VMType> type_;
      const std::shared_ptr<Expression> obj_expr_;
      Value* const delegate_;
    };

    CHECK(field->type.type == VMType::FUNCTION);
    CHECK_GE(field->type.type_arguments.size(), 2);
    CHECK_EQ(field->type.type_arguments[1], type);

    return std::make_unique<BindObjectExpression>(object->Clone(), field);
  }

  CHECK(!errors.empty());
  compilation->errors.push_back(errors[0]);
  return nullptr;
}

void Call(Value* func, vector<Value::Ptr> args,
          std::function<void(Value::Ptr)> consumer,
          std::function<void(std::function<void()>)> yield_callback) {
  std::vector<std::unique_ptr<Expression>> args_expr;
  for (auto& a : args) {
    args_expr.push_back(NewConstantExpression(std::move(a)));
  }
  // TODO: Use unique_ptr and capture by std::move.
  std::shared_ptr<Expression> function_expr =
      NewFunctionCall(NewConstantExpression(Value::NewFunction(
                          func->type.type_arguments, func->callback)),
                      std::move(args_expr));
  Evaluate(
      function_expr.get(), nullptr,
      [function_expr, consumer](Value::Ptr value) {
        consumer(std::move(value));
      },
      yield_callback);
}

}  // namespace vm
}  // namespace afc
