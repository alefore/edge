#include "src/vm/assign_expression.h"

#include <glog/logging.h>

#include "src/language/gc_view.h"
#include "src/language/wstring.h"
#include "src/vm/compilation.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
namespace {

using ::operator<<;

class AssignExpression : public Expression {
 public:
  enum class AssignmentType { kDefine, kAssign };

 private:
  const AssignmentType assignment_type_;
  const Identifier symbol_;
  const PurityType purity_;
  const NonNull<std::shared_ptr<Expression>> value_;

 public:
  AssignExpression(AssignmentType assignment_type, Identifier symbol,
                   PurityType purity,
                   NonNull<std::shared_ptr<Expression>> value)
      : assignment_type_(assignment_type),
        symbol_(std::move(symbol)),
        purity_(CombinePurityType({std::move(purity), value->purity()})),
        value_(std::move(value)) {}

  std::vector<Type> Types() override { return value_->Types(); }
  std::unordered_set<Type> ReturnTypes() const override {
    return value_->ReturnTypes();
  }

  PurityType purity() override { return purity_; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    return trampoline.Bounce(value_, type)
        .Transform(
            [&trampoline, symbol = symbol_,
             assignment_type = assignment_type_](EvaluationOutput value_output)
                -> language::ValueOrError<EvaluationOutput> {
              switch (value_output.type) {
                case EvaluationOutput::OutputType::kReturn:
                  return Success(std::move(value_output));
                case EvaluationOutput::OutputType::kContinue:
                  DVLOG(3) << "Setting value for: " << symbol;
                  DVLOG(4) << "Value: " << value_output.value.ptr().value();
                  if (assignment_type == AssignmentType::kDefine) {
                    trampoline.environment().ptr()->Define(symbol,
                                                           value_output.value);
                  } else {
                    trampoline.environment().ptr()->Assign(symbol,
                                                           value_output.value);
                  }
                  return Success(
                      EvaluationOutput::New(std::move(value_output.value)));
              }
              language::Error error(L"Unhandled OutputType case.");
              LOG(FATAL) << error;
              return error;
            });
  }
};
}  // namespace

std::optional<Type> NewDefineTypeExpression(Compilation& compilation,
                                            Identifier type, Identifier symbol,
                                            std::optional<Type> default_type) {
  Type type_def;
  if (type == IdentifierAuto()) {
    if (default_type == std::nullopt) {
      compilation.AddError(Error(L"Unable to deduce type."));
      return std::nullopt;
    }
    type_def = default_type.value();
  } else {
    auto type_ptr = compilation.environment.ptr()->LookupType(type);
    if (type_ptr == nullptr) {
      compilation.AddError(Error(L"Unknown type: `" + type.read() +
                                 L"` for symbol `" + symbol.read() + L"`."));
      return std::nullopt;
    }
    type_def = *type_ptr;
  }
  compilation.environment.ptr()->Define(symbol,
                                        Value::New(compilation.pool, type_def));
  return type_def;
}

std::unique_ptr<Expression> NewDefineExpression(
    Compilation& compilation, Identifier type, Identifier symbol,
    std::unique_ptr<Expression> value) {
  if (value == nullptr) return nullptr;
  std::optional<Type> default_type;
  if (type == IdentifierAuto()) {
    auto types = value->Types();
    if (types.size() != 1) {
      compilation.AddError(Error(L"Unable to deduce type for symbol: `" +
                                 symbol.read() + L"`."));
      return nullptr;
    }
    default_type = *types.cbegin();
  }
  std::optional<Type> vmtype =
      NewDefineTypeExpression(compilation, type, symbol, default_type);
  if (vmtype == std::nullopt) return nullptr;
  if (!value->SupportsType(*vmtype)) {
    compilation.AddError(
        Error{LazyString{L"Unable to assign a value to a variable of type \""} +
              ToString(*vmtype) + LazyString{L"\". Value types: "} +
              TypesToString(value->Types())});
    return nullptr;
  }
  return std::make_unique<AssignExpression>(
      AssignExpression::AssignmentType::kDefine, std::move(symbol),
      PurityType{.writes_local_variables = true},
      NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(value)));
}

std::unique_ptr<Expression> NewAssignExpression(
    Compilation& compilation, Identifier symbol,
    std::unique_ptr<Expression> value) {
  if (value == nullptr) return nullptr;
  static const vm::Namespace kEmptyNamespace;
  std::vector<Environment::LookupResult> variables =
      compilation.environment.ptr()->PolyLookup(kEmptyNamespace, symbol);
  if (variables.empty()) {
    compilation.AddError(
        Error(L"Variable not found: \"" + symbol.read() + L"\""));
    return nullptr;
  }

  return VisitOptional(
      [&value, &symbol](const Environment::LookupResult& lookup_result) {
        return std::make_unique<AssignExpression>(
            AssignExpression::AssignmentType::kAssign, symbol,
            std::invoke([&lookup_result] {
              switch (lookup_result.scope) {
                case Environment::LookupResult::VariableScope::kLocal:
                  return PurityType{.writes_local_variables = true};
                case Environment::LookupResult::VariableScope::kGlobal:
                  return PurityType{.writes_external_outputs = true};
              }
              LOG(FATAL) << "Invalid scope.";
              return kPurityTypeUnknown;
            }),
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(value)));
      },
      [&] {
        compilation.AddError(Error{
            LazyString{L"Unable to assign a value to a variable supporting "
                       L"types: \""} +
            TypesToString(value->Types()) + LazyString{L"\". Value types: "} +
            TypesToString(container::MaterializeVector(
                std::move(variables) |
                std::views::transform(&Environment::LookupResult::value) |
                gc::view::Value | std::views::transform(&Value::type)))});

        return nullptr;
      },
      container::FindFirstIf(
          variables, [&value](Environment::LookupResult lookup_result) {
            return value->SupportsType(lookup_result.value.ptr()->type);
          }));
}

}  // namespace afc::vm
