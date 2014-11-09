#include "function_call.h"

#include <cassert>

#include <glog/logging.h>

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

  void Evaluate(OngoingEvaluation* evaluation) {
    DVLOG(3) << "Function call evaluation starts.";
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [this, advancer](OngoingEvaluation* inner_evaluation) {
          DVLOG(5) << "Evaluating function parameters.";
          assert(inner_evaluation->value != nullptr);
          auto func = std::move(inner_evaluation->value);
          if (args_->empty()) {
            inner_evaluation->advancer = advancer;
            inner_evaluation->value =
                func->callback(vector<unique_ptr<Value>>());
            return;
          }
          inner_evaluation->advancer = CaptureArgs(
              advancer, new vector<unique_ptr<Value>>(),
              shared_ptr<Value>(func.release()));
          args_->at(0)->Evaluate(inner_evaluation);
        };
    func_->Evaluate(evaluation);
  }

 private:
  std::function<void(OngoingEvaluation*)> CaptureArgs(
      std::function<void(OngoingEvaluation*)> advancer,
      vector<unique_ptr<Value>>* values, shared_ptr<Value> func) {
    return [this, advancer, values, func]
        (OngoingEvaluation* evaluation) {
          DVLOG(5) << "Received results of parameter: " << values->size() + 1;
          values->push_back(std::move(evaluation->value));
          if (values->size() == args_->size()) {
            // TODO: Delete values, we're memory leaking here.
            DVLOG(4) << "No more parameters, performing function call.";
            evaluation->advancer = advancer;
            evaluation->value = func->callback(std::move(*values));
            return;
          }
          evaluation->advancer = CaptureArgs(advancer, values, func);
          args_->at(values->size())->Evaluate(evaluation);
        };
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
