#include "assign_expression.h"

#include <glog/logging.h>

#include "compilation.h"
#include "evaluation.h"
#include "../public/environment.h"
#include "../public/value.h"
#include "wstring.h"

namespace afc {
namespace vm {

namespace {

// TODO: Don't pass symbol by const reference.
class AssignExpression : public Expression {
 public:
  AssignExpression(const wstring& symbol, unique_ptr<Expression> value)
      : symbol_(symbol), value_(std::move(value)) {}

  const VMType& type() { return value_->type(); }

  void Evaluate(OngoingEvaluation* evaluation) {
    auto advancer = evaluation->advancer;
    evaluation->advancer =
        [this, advancer](OngoingEvaluation* inner_evaluation) {
          DVLOG(3) << "Setting value for: " << symbol_;
          DVLOG(4) << "Value: " << *inner_evaluation->value;
          inner_evaluation->environment->Define(
              symbol_,
              unique_ptr<Value>(new Value(*inner_evaluation->value)));
          inner_evaluation->advancer = advancer;
        };
    value_->Evaluate(evaluation);
  }

 private:
  const wstring symbol_;
  unique_ptr<Expression> value_;
};

}

// TODO: Don't pass type/symbol by const reference.
unique_ptr<Expression> NewAssignExpression(
    Compilation* compilation, const wstring& type, const wstring& symbol,
    unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  const VMType* type_def = compilation->environment->LookupType(type);
  if (type_def == nullptr) {
    compilation->errors.push_back(L"Unknown type: \"" + symbol + L"\"");
    return nullptr;
  }
  compilation->environment
      ->Define(symbol, unique_ptr<Value>(new Value(value->type())));
  if (!(*type_def == value->type())) {
    compilation->errors.push_back(
        L"Unable to assign a value of type \"" + value->type().ToString()
        + L"\" to a variable of type \"" + type_def->ToString() + L"\".");
    return nullptr;
  }
  return unique_ptr<Expression>(new AssignExpression(symbol, std::move(value)));
}

unique_ptr<Expression> NewAssignExpression(
    Compilation* compilation, const wstring& symbol,
    unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  auto obj = compilation->environment->Lookup(symbol);
  if (obj == nullptr) {
    compilation->errors.push_back(L"Variable not found: \"" + symbol + L"\"");
    return nullptr;
  }
  if (!(obj->type == value->type())) {
    compilation->errors.push_back(
        L"Unable to assign a value of type \"" + value->type().ToString()
        + L"\" to a variable of type \"" + obj->type.ToString() + L"\".");
    return nullptr;
  }

  return unique_ptr<Expression>(new AssignExpression(symbol, std::move(value)));
}

}  // namespace vm
}  // namespace afc
