#include "if_expression.h"

#include <glog/logging.h>

#include "../internal/compilation.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class IfExpression : public Expression {
 public:
  IfExpression(std::shared_ptr<Expression> cond,
               std::shared_ptr<Expression> true_case,
               std::shared_ptr<Expression> false_case,
               std::unordered_set<VMType> return_types)
      : cond_(std::move(cond)),
        true_case_(std::move(true_case)),
        false_case_(std::move(false_case)),
        return_types_(std::move(return_types)) {
    CHECK(cond_ != nullptr);
    CHECK(cond_->IsBool());
    CHECK(true_case_ != nullptr);
    CHECK(false_case_ != nullptr);
  }

  std::vector<VMType> Types() override { return true_case_->Types(); }

  std::unordered_set<VMType> ReturnTypes() const override {
    return return_types_;
  }

  PurityType purity() override {
    return cond_->purity() == PurityType::kPure &&
                   true_case_->purity() == PurityType::kPure &&
                   false_case_->purity() == PurityType::kPure
               ? PurityType::kPure
               : PurityType::kUnknown;
  }

  futures::Value<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                            const VMType& type) override {
    return trampoline->Bounce(cond_.get(), VMType::Bool())
        .Transform([type, true_case = true_case_, false_case = false_case_,
                    trampoline](EvaluationOutput cond_output) {
          switch (cond_output.type) {
            case EvaluationOutput::OutputType::kReturn:
            case EvaluationOutput::OutputType::kAbort:
              return futures::Past(std::move(cond_output));
            case EvaluationOutput::OutputType::kContinue:
              return trampoline->Bounce(cond_output.value->boolean
                                            ? true_case.get()
                                            : false_case.get(),
                                        type);
          }
          auto error = afc::editor::Error(L"Unhandled OutputType case.");
          LOG(FATAL) << error;
          return futures::Past(EvaluationOutput::Abort(error));
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<IfExpression>(cond_, true_case_, false_case_,
                                          return_types_);
  }

 private:
  const std::shared_ptr<Expression> cond_;
  const std::shared_ptr<Expression> true_case_;
  const std::shared_ptr<Expression> false_case_;
  const std::unordered_set<VMType> return_types_;
};

}  // namespace

std::unique_ptr<Expression> NewIfExpression(
    Compilation* compilation, std::unique_ptr<Expression> condition,
    std::unique_ptr<Expression> true_case,
    std::unique_ptr<Expression> false_case) {
  if (condition == nullptr || true_case == nullptr || false_case == nullptr) {
    return nullptr;
  }

  if (!condition->IsBool()) {
    compilation->errors.push_back(
        L"Expected bool value for condition of \"if\" expression but found " +
        TypesToString(condition->Types()) + L".");
    return nullptr;
  }

  if (!(true_case->Types() == false_case->Types())) {
    compilation->errors.push_back(
        L"Type mismatch between branches of conditional expression: " +
        TypesToString(true_case->Types()) + L" and " +
        TypesToString(false_case->Types()) + L".");
    return nullptr;
  }

  std::wstring error;
  auto return_types = CombineReturnTypes(true_case->ReturnTypes(),
                                         false_case->ReturnTypes(), &error);
  if (!return_types.has_value()) {
    compilation->errors.push_back(error);
    return nullptr;
  }

  return std::make_unique<IfExpression>(
      std::move(condition), std::move(true_case), std::move(false_case),
      std::move(return_types.value()));
}

}  // namespace vm
}  // namespace afc
