#include "assign_expression.h"

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"
#include "wstring.h"

namespace afc::vm {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;

namespace gc = language::gc;

class AssignExpression : public Expression {
 public:
  enum class AssignmentType { kDefine, kAssign };

  AssignExpression(AssignmentType assignment_type, wstring symbol,
                   NonNull<std::shared_ptr<Expression>> value)
      : assignment_type_(assignment_type),
        symbol_(std::move(symbol)),
        value_(std::move(value)) {}

  std::vector<VMType> Types() override { return value_->Types(); }
  std::unordered_set<VMType> ReturnTypes() const override {
    return value_->ReturnTypes();
  }

  PurityType purity() override { return PurityType::kUnknown; }

  futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const VMType& type) override {
    return trampoline.Bounce(*value_, type)
        .Transform(
            [&trampoline, symbol = symbol_,
             assignment_type = assignment_type_](EvaluationOutput value_output)
                -> language::ValueOrError<EvaluationOutput> {
              switch (value_output.type) {
                case EvaluationOutput::OutputType::kReturn:
                  return Success(std::move(value_output));
                case EvaluationOutput::OutputType::kContinue:
                  DVLOG(3) << "Setting value for: " << symbol;
                  DVLOG(4) << "Value: " << value_output.value.value().value();
                  if (assignment_type == AssignmentType::kDefine) {
                    trampoline.environment().value()->Define(
                        symbol, value_output.value);
                  } else {
                    trampoline.environment().value()->Assign(
                        symbol, value_output.value);
                  }
                  return Success(
                      EvaluationOutput::New(std::move(value_output.value)));
              }
              language::Error error(L"Unhandled OutputType case.");
              LOG(FATAL) << error;
              return error;
            });
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<AssignExpression>(assignment_type_, symbol_,
                                               value_);
  }

 private:
  const AssignmentType assignment_type_;
  const wstring symbol_;
  const NonNull<std::shared_ptr<Expression>> value_;
};
}  // namespace

std::optional<VMType> NewDefineTypeExpression(
    Compilation* compilation, std::wstring type, std::wstring symbol,
    std::optional<VMType> default_type) {
  VMType type_def;
  if (type == L"auto") {
    if (default_type == std::nullopt) {
      compilation->errors.push_back(L"Unable to deduce type.");
      return std::nullopt;
    }
    type_def = default_type.value();
  } else {
    auto type_ptr = compilation->environment.value()->LookupType(type);
    if (type_ptr == nullptr) {
      compilation->errors.push_back(L"Unknown type: `" + type +
                                    L"` for symbol `" + symbol + L"`.");
      return std::nullopt;
    }
    type_def = *type_ptr;
  }
  compilation->environment.value()->Define(
      symbol, compilation->pool.NewRoot(MakeNonNullUnique<Value>(type_def)));
  return type_def;
}

std::unique_ptr<Expression> NewDefineExpression(
    Compilation* compilation, std::wstring type, std::wstring symbol,
    std::unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  std::optional<VMType> default_type;
  if (type == L"auto") {
    auto types = value->Types();
    if (types.size() != 1) {
      compilation->errors.push_back(L"Unable to deduce type for symbol: `" +
                                    symbol + L"`.");
      return nullptr;
    }
    default_type = *types.cbegin();
  }
  auto vmtype =
      NewDefineTypeExpression(compilation, type, symbol, default_type);
  if (vmtype == std::nullopt) return nullptr;
  if (!value->SupportsType(*vmtype)) {
    compilation->errors.push_back(
        L"Unable to assign a value to a variable of type \"" +
        vmtype->ToString() + L"\". Value types: " +
        TypesToString(value->Types()));
    return nullptr;
  }
  return std::make_unique<AssignExpression>(
      AssignExpression::AssignmentType::kDefine, std::move(symbol),
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(value)));
}

std::unique_ptr<Expression> NewAssignExpression(
    Compilation* compilation, std::wstring symbol,
    std::unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  std::vector<gc::Root<Value>> variables;
  compilation->environment.value()->PolyLookup(symbol, &variables);
  for (gc::Root<Value>& v : variables) {
    if (value->SupportsType(v.value()->type)) {
      return std::make_unique<AssignExpression>(
          AssignExpression::AssignmentType::kAssign, symbol,
          NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(value)));
    }
  }

  if (variables.empty()) {
    compilation->errors.push_back(L"Variable not found: \"" + symbol + L"\"");
    return nullptr;
  }

  std::vector<VMType> variable_types;
  for (gc::Root<Value>& v : variables) {
    variable_types.push_back(v.value()->type);
  }

  compilation->errors.push_back(
      L"Unable to assign a value to a variable supporting types: \"" +
      TypesToString(value->Types()) + L"\". Value types: " +
      TypesToString(variable_types));

  return nullptr;
}

}  // namespace afc::vm
