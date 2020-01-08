#include "while_expression.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "../public/vm.h"
#include "append_expression.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

class WhileExpression : public Expression {
 public:
  WhileExpression(std::shared_ptr<Expression> condition,
                  std::shared_ptr<Expression> body)
      : condition_(std::move(condition)), body_(std::move(body)) {
    CHECK(condition_ != nullptr);
    CHECK(body_ != nullptr);
  }

  std::vector<VMType> Types() { return {VMType::Void()}; }

  std::unordered_set<VMType> ReturnTypes() const override {
    return body_->ReturnTypes();
  }

  futures::DelayedValue<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                                   const VMType&) override {
    DVLOG(4) << "Starting iteration.";
    futures::Future<EvaluationOutput> output;
    Iterate(trampoline, condition_, body_, output.consumer());
    return output.value();
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<WhileExpression>(condition_, body_);
  }

 private:
  static void Iterate(
      Trampoline* trampoline, std::shared_ptr<Expression> condition,
      std::shared_ptr<Expression> body,
      futures::DelayedValue<EvaluationOutput>::Consumer consumer) {
    trampoline->Bounce(condition.get(), VMType::Bool())
        .SetConsumer([condition, body, consumer,
                      trampoline](EvaluationOutput condition_output) {
          CHECK(condition_output.value->IsBool());
          if (!condition_output.value->boolean) {
            DVLOG(3) << "Iteration is done.";
            consumer(EvaluationOutput::New(Value::NewVoid()));
            return;
          }

          DVLOG(5) << "Iterating...";
          trampoline->Bounce(body.get(), body->Types()[0])
              .SetConsumer([condition, body, consumer,
                            trampoline](EvaluationOutput body_output) {
                if (body_output.type == EvaluationOutput::OutputType::kReturn) {
                  consumer(std::move(body_output));
                } else {
                  Iterate(trampoline, std::move(condition), std::move(body),
                          std::move(consumer));
                }
              });
        });
  }

  const std::shared_ptr<Expression> condition_;
  const std::shared_ptr<Expression> body_;
};

}  // namespace

std::unique_ptr<Expression> NewWhileExpression(
    Compilation* compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> body) {
  if (condition == nullptr || body == nullptr) {
    return nullptr;
  }
  if (!condition->IsBool()) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"while\" loop but found: " +
        TypesToString(condition->Types()) + L".");
    return nullptr;
  }

  return std::make_unique<WhileExpression>(std::move(condition),
                                           std::move(body));
}

std::unique_ptr<Expression> NewForExpression(
    Compilation* compilation, std::unique_ptr<Expression> init,
    std::unique_ptr<Expression> condition, std::unique_ptr<Expression> update,
    std::unique_ptr<Expression> body) {
  if (init == nullptr || condition == nullptr || update == nullptr ||
      body == nullptr) {
    return nullptr;
  }
  return NewAppendExpression(
      std::move(init),
      NewWhileExpression(
          compilation, std::move(condition),
          NewAppendExpression(std::move(body), std::move(update))));
}

}  // namespace vm
}  // namespace afc
