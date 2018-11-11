#include "assign_expression.h"

#include <glog/logging.h>

#include "compilation.h"
#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
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

  void Evaluate(Trampoline* trampoline) override {
    auto expression = value_;
    auto symbol = symbol_;
    trampoline->Bounce(expression.get(),
        [expression, symbol](std::unique_ptr<Value> value,
                             Trampoline* trampoline) {
          DVLOG(3) << "Setting value for: " << symbol;
          DVLOG(4) << "Value: " << *value;
          trampoline->environment()->Define(symbol, std::move(value));
          trampoline->Continue(Value::NewVoid());
        });
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<AssignExpression>(symbol_, value_->Clone());
  }

 private:
  const wstring symbol_;
  const std::shared_ptr<Expression> value_;
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
  compilation->environment->Define(symbol,
                                   std::make_unique<Value>(value->type()));
  if (!(*type_def == value->type())) {
    compilation->errors.push_back(
        L"Unable to assign a value of type \"" + value->type().ToString()
        + L"\" to a variable of type \"" + type_def->ToString() + L"\".");
    return nullptr;
  }
  return std::make_unique<AssignExpression>(symbol, std::move(value));
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

  return std::make_unique<AssignExpression>(symbol, std::move(value));
}

}  // namespace vm
}  // namespace afc
