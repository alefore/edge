#include "assign_expression.h"

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"
#include "wstring.h"

namespace afc {
namespace vm {

namespace {

// TODO: Don't pass symbol by const reference.
class AssignExpression : public Expression {
 public:
  AssignExpression(const wstring& symbol, unique_ptr<Expression> value)
      : symbol_(symbol), value_(std::move(value)) {}

  std::vector<VMType> Types() override { return value_->Types(); }

  void Evaluate(Trampoline* trampoline, const VMType&) override {
    auto expression = value_;
    auto symbol = symbol_;
    trampoline->Bounce(
        expression.get(), [expression, symbol](std::unique_ptr<Value> value,
                                               Trampoline* trampoline) {
          DVLOG(3) << "Setting value for: " << symbol;
          DVLOG(4) << "Value: " << *value;
          trampoline->environment()->Assign(symbol, std::move(value));
          // TODO: This seems wrong: shouldn't it be `value`?
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

}  // namespace

// TODO: Don't pass type/symbol by const reference.
unique_ptr<Expression> NewAssignExpression(Compilation* compilation,
                                           const wstring& type,
                                           const wstring& symbol,
                                           unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  const VMType* type_def = compilation->environment->LookupType(type);
  if (type_def == nullptr) {
    compilation->errors.push_back(L"Unknown type: \"" + type +
                                  L"\" for symbol \"" + symbol + L"\".");
    return nullptr;
  }
  if (!value->SupportsType(*type_def)) {
    compilation->errors.push_back(
        L"Unable to assign a value to a variable of type \"" +
        type_def->ToString() + L"\". Value types: " +
        TypesToString(value->Types()));
    return nullptr;
  }
  compilation->environment->Define(symbol, std::make_unique<Value>(*type_def));
  return std::make_unique<AssignExpression>(symbol, std::move(value));
}

unique_ptr<Expression> NewAssignExpression(Compilation* compilation,
                                           const wstring& symbol,
                                           unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  std::vector<Value*> variables;
  compilation->environment->PolyLookup(symbol, &variables);
  for (auto& v : variables) {
    if (value->SupportsType(v->type)) {
      return std::make_unique<AssignExpression>(symbol, std::move(value));
    }
  }

  if (variables.empty()) {
    compilation->errors.push_back(L"Variable not found: \"" + symbol + L"\"");
    return nullptr;
  }

  std::vector<VMType> variable_types;
  for (auto& v : variables) {
    variable_types.push_back(v->type);
  }

  compilation->errors.push_back(
      L"Unable to assign a value to a variable supporting types: \"" +
      TypesToString(value->Types()) + L"\". Value types: " +
      TypesToString(variable_types));

  return nullptr;
}

}  // namespace vm
}  // namespace afc
