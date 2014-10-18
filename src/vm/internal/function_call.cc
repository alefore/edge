#include "function_call.h"

#include <cassert>

#include "evaluation.h"
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
    assert(func_ != nullptr);
    assert(args_ != nullptr);
  }

  const VMType& type() {
    return func_->type().type_arguments[0];
  }

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    return func_->Evaluate(Evaluation(evaluation, Continuation(
        [this, evaluation](unique_ptr<Value> func) {
          assert(func != nullptr);
          if (args_->empty()) {
            return make_pair(evaluation.continuation,
                             func->callback(vector<unique_ptr<Value>>()));
          }
          return args_->at(0)->Evaluate(Evaluation(evaluation,
              CaptureArgs(evaluation, new vector<unique_ptr<Value>>,
                          shared_ptr<Value>(func.release()))));
        })));
  }

 private:
  Continuation CaptureArgs(const Evaluation& evaluation,
                           vector<unique_ptr<Value>>* values,
                           shared_ptr<Value> func) {
    return Continuation(
        [this, evaluation, func, values](unique_ptr<Value> value) {
          values->push_back(std::move(value));
          if (values->size() == args_->size()) {
            // TODO: Delete values, we're memory leaking here.
            return make_pair(evaluation.continuation,
                             func->callback(std::move(*values)));
          }
          return args_->at(values->size())->Evaluate(Evaluation(evaluation,
              CaptureArgs(evaluation, values, func)));
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

}  // namespace vm
}  // namespace afc
