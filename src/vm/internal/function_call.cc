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

  void Evaluate(OngoingEvaluation* evaluation) {
    DVLOG(3) << "Function call evaluation starts.";
    EvaluateExpression(evaluation, func_.get(),
        [this, evaluation](std::unique_ptr<Value> callback) {
          DVLOG(6) << "Got function: " << *callback;
          CaptureArgs(evaluation, std::make_shared<vector<unique_ptr<Value>>>(),
              std::move(callback));
        });
  }

 private:
  void CaptureArgs(OngoingEvaluation* evaluation,
      std::shared_ptr<vector<unique_ptr<Value>>> values,
      std::shared_ptr<Value> callback) {
    CHECK(callback != nullptr);
    CHECK_EQ(callback->type.type, VMType::FUNCTION);
    CHECK(callback->callback);

    DVLOG(5) << "Evaluating function parameters, args: " << args_->size();
    if (values->size() == args_->size()) {
      DVLOG(4) << "No more parameters, performing function call.";
      auto original_consumer = std::move(evaluation->consumer);
      auto original_return_consumer = std::move(evaluation->return_consumer);
      evaluation->return_consumer =
          [evaluation, original_consumer, original_return_consumer](
              Value::Ptr value) {
            LOG(INFO) << "Got returned value: " << *value;
            auto evaluation_copy = evaluation;
            auto consumer_copy = original_consumer;
            auto return_consumer_copy = original_return_consumer;

            evaluation_copy->consumer = consumer_copy;
            evaluation_copy->return_consumer = return_consumer_copy;
            evaluation_copy->consumer(std::move(value));
          };
      evaluation->consumer = evaluation->return_consumer;
      callback->callback(std::move(*values), evaluation);
      return;
    }
    EvaluateExpression(evaluation, args_->at(values->size()).get(),
        [this, evaluation, values, callback](std::unique_ptr<Value> value) {
          CHECK(values != nullptr);
          DVLOG(5) << "Received results of parameter " << values->size() + 1
                   << ": " << *value;
          values->push_back(std::move(value));
          DVLOG(6) << "Recursive call.";
          CHECK(callback != nullptr);
          CHECK_EQ(callback->type.type, VMType::FUNCTION);
          CHECK(callback->callback);
          CaptureArgs(evaluation, values, callback);
        });
  }

  unique_ptr<Expression> func_;
  unique_ptr<vector<unique_ptr<Expression>>> args_;
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
