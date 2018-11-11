#include "../public/function_call.h"

#include <glog/logging.h>

#include "evaluation.h"
#include "../public/constant_expression.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

class FunctionCall : public Expression {
 public:
  FunctionCall(unique_ptr<Expression> func,
               unique_ptr<vector<unique_ptr<Expression>>> args)
      : func_(std::move(func)), args_(std::move(args)) {
    CHECK(func_ != nullptr);
    CHECK(args_ != nullptr);
  }

  const VMType& type() {
    return func_->type().type_arguments[0];
  }

  void Evaluate(Trampoline* trampoline) {
    DVLOG(3) << "Function call evaluation starts.";
    trampoline->Bounce(func_.get(),
        [this](std::unique_ptr<Value> callback, Trampoline* trampoline) {
          DVLOG(6) << "Got function: " << *callback;
          CaptureArgs(trampoline, std::make_shared<vector<unique_ptr<Value>>>(),
              std::move(callback));
        });
  }

 private:
  void CaptureArgs(Trampoline* trampoline,
                   std::shared_ptr<vector<unique_ptr<Value>>> values,
                   std::shared_ptr<Value> callback) {
    CHECK(callback != nullptr);
    CHECK_EQ(callback->type.type, VMType::FUNCTION);
    CHECK(callback->callback);

    DVLOG(5) << "Evaluating function parameters, args: " << args_->size();
    if (values->size() == args_->size()) {
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
    trampoline->Bounce(args_->at(values->size()).get(),
        [this, values, callback](std::unique_ptr<Value> value,
                                 Trampoline* trampoline) {
          CHECK(values != nullptr);
          DVLOG(5) << "Received results of parameter " << values->size() + 1
                   << " (of " << args_->size() << "): " << *value;
          values->push_back(std::move(value));
          DVLOG(6) << "Recursive call.";
          CHECK(callback != nullptr);
          CHECK_EQ(callback->type.type, VMType::FUNCTION);
          CHECK(callback->callback);
          CaptureArgs(trampoline, values, callback);
        });
  }

  const unique_ptr<Expression> func_;
  const unique_ptr<vector<unique_ptr<Expression>>> args_;
};

}  // namespace

unique_ptr<Expression> NewFunctionCall(
    unique_ptr<Expression> func,
    unique_ptr<vector<unique_ptr<Expression>>> args) {
  return unique_ptr<Expression>(
      new FunctionCall(std::move(func), std::move(args)));
}

void Call(Value* func, vector<Value::Ptr> args,
          std::function<void(Value::Ptr)> consumer) {
  std::unique_ptr<std::vector<std::unique_ptr<Expression>>> args_expr(
      new std::vector<std::unique_ptr<Expression>>());
  for (auto& a : args) {
    args_expr->push_back(NewConstantExpression(std::move(a)));
  }
  shared_ptr<Expression> function_expr = NewFunctionCall(
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
