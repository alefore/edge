#include "../public/function_call.h"

#include <glog/logging.h>

#include "../public/constant_expression.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class FunctionCall : public Expression {
 public:
  FunctionCall(std::shared_ptr<Expression> func,
               std::shared_ptr<std::vector<std::unique_ptr<Expression>>> args)
      : func_(std::move(func)), args_(std::move(args)) {
    CHECK(func_ != nullptr);
    CHECK(args_ != nullptr);
  }

  const VMType& type() {
    return func_->type().type_arguments[0];
  }

  void Evaluate(Trampoline* trampoline) {
    DVLOG(3) << "Function call evaluation starts.";
    auto args_types = args_;
    auto func = func_;
    trampoline->Bounce(func_.get(),
        [func, args_types](std::unique_ptr<Value> callback,
                           Trampoline* trampoline) {
          DVLOG(6) << "Got function: " << *callback;
          CaptureArgs(trampoline, args_types,
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
      std::function<void(Trampoline*)> original_state = trampoline->Save();
      trampoline->SetReturnContinuation(
          [original_state](std::unique_ptr<Value> value,
                           Trampoline* trampoline) {
            CHECK(value != nullptr);
            LOG(INFO) << "Got returned value: " << *value;
            original_state(trampoline);
            trampoline->Continue(std::move(value));
          });
      trampoline->SetContinuation(trampoline->return_continuation());
      callback->callback(std::move(*values), trampoline);
      return;
    }
    trampoline->Bounce(args_types->at(values->size()).get(),
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

void Call(Value* func, vector<Value::Ptr> args,
          std::function<void(Value::Ptr)> consumer) {
  std::vector<std::unique_ptr<Expression>> args_expr;
  for (auto& a : args) {
    args_expr.push_back(NewConstantExpression(std::move(a)));
  }
  // TODO: Use unique_ptr and capture by std::move.
  std::shared_ptr<Expression> function_expr = NewFunctionCall(
      NewConstantExpression(Value::NewFunction(func->type.type_arguments,
                                               func->callback)),
      std::move(args_expr));
  Evaluate(function_expr.get(), nullptr,
           [function_expr, consumer](Value::Ptr value) {
             consumer(std::move(value));
           });
}

}  // namespace vm
}  // namespace afc
