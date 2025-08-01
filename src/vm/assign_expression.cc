#include "src/vm/assign_expression.h"

#include <glog/logging.h>

#include "src/language/gc_view.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/vm/compilation.h"
#include "src/vm/delegating_expression.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;
using afc::language::lazy_string::ToSingleLine;

namespace afc::vm {
namespace {

using ::operator<<;

class AssignExpression : public Expression {
  struct ConstructorAccessTag {};

 public:
  enum class AssignmentType { kDefine, kAssign };

 private:
  const AssignmentType assignment_type_;
  const Identifier symbol_;
  const PurityType purity_;
  const NonNull<std::shared_ptr<Expression>> value_;

 public:
  static gc::Root<AssignExpression> New(
      gc::Pool& pool, AssignmentType assignment_type, Identifier symbol,
      PurityType purity, NonNull<std::shared_ptr<Expression>> value) {
    return pool.NewRoot(language::MakeNonNullUnique<AssignExpression>(
        assignment_type, std::move(symbol), std::move(purity),
        std::move(value)));
  }

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
              language::Error error{LazyString{L"Unhandled OutputType case."}};
              LOG(FATAL) << error;
              return error;
            });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }
};

class StackFrameAssign : public Expression {
  struct ConstructorAccessTag {};

  const size_t index_;
  NonNull<std::shared_ptr<Expression>> value_expression_;

 public:
  static gc::Root<StackFrameAssign> New(
      gc::Pool& pool, size_t index,
      NonNull<std::unique_ptr<Expression>> value_expression) {
    return pool.NewRoot(language::MakeNonNullUnique<StackFrameAssign>(
        index, std::move(value_expression)));
  }

  StackFrameAssign(size_t index,
                   NonNull<std::unique_ptr<Expression>> value_expression)
      : index_(index), value_expression_(std::move(value_expression)) {}

  std::vector<Type> Types() override { return value_expression_->Types(); }
  std::unordered_set<Type> ReturnTypes() const override {
    return value_expression_->ReturnTypes();
  }

  PurityType purity() override {
    return PurityType{.writes_local_variables = true};
  }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    return trampoline.Bounce(value_expression_, type)
        .Transform([&trampoline, index = index_](EvaluationOutput value_output)
                       -> language::ValueOrError<EvaluationOutput> {
          switch (value_output.type) {
            case EvaluationOutput::OutputType::kReturn:
              return Success(std::move(value_output));
            case EvaluationOutput::OutputType::kContinue:
              trampoline.stack().current_frame().get(index) =
                  value_output.value.ptr();
              return Success(
                  EvaluationOutput::New(std::move(value_output.value)));
          }
          language::Error error{LazyString{L"Unhandled OutputType case."}};
          LOG(FATAL) << error;
          return error;
        });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }
};
}  // namespace

ValueOrError<Type> DefineUninitializedVariable(
    Environment& environment, Identifier type, Identifier symbol,
    std::optional<Type> default_type) {
  Type type_def;
  if (type == IdentifierAuto()) {
    if (default_type == std::nullopt)
      return Error{LazyString{L"Unable to deduce type."}};
    type_def = default_type.value();
  } else if (auto type_ptr = environment.LookupType(type); type_ptr != nullptr)
    type_def = *type_ptr;
  else {
    return Error{LazyString{L"Unknown type: "} +
                 QuoteExpr(language::lazy_string::ToSingleLine(type)) +
                 LazyString{L" for symbol "} +
                 QuoteExpr(language::lazy_string::ToSingleLine(symbol)) +
                 LazyString{L"."}};
  }
  environment.DefineUninitialized(symbol, type_def);
  return type_def;
}

std::optional<gc::Root<Expression>> NewDefineExpression(
    Compilation& compilation, Identifier type, Identifier symbol,
    std::optional<gc::Root<Expression>> value) {
  if (value == std::nullopt) return std::nullopt;
  std::optional<Type> default_type;
  if (type == IdentifierAuto()) {
    if (auto types = value.value()->Types(); types.size() == 1)
      default_type = *types.cbegin();
    else {
      compilation.AddError(
          Error{LazyString{L"Unable to deduce type for symbol: `"} +
                ToLazyString(symbol) + LazyString{L"`."}});
      return std::nullopt;
    }
  }
  return std::visit(
      overload{
          [&](Type final_type) -> std::optional<gc::Root<Expression>> {
            if (!value.value()->SupportsType(final_type)) {
              compilation.AddError(Error{
                  LazyString{
                      L"Unable to assign a value to a variable of type "} +
                  ToQuotedSingleLine(final_type) +
                  LazyString{L". Value types: "} +
                  TypesToString(value.value()->Types()) + LazyString{L"."}});
              return std::nullopt;
            }
            return compilation.pool.NewRoot(MakeNonNullUnique<AssignExpression>(
                AssignExpression::AssignmentType::kDefine, std::move(symbol),
                PurityType{.writes_local_variables = true},
                NewDelegatingExpression(std::move(value.value()))));
          },
          [&](Error error) -> std::optional<gc::Root<Expression>> {
            compilation.AddError(error);
            return std::nullopt;
          }},
      DefineUninitializedVariable(compilation.environment.value(), type, symbol,
                                  default_type));
}

std::optional<gc::Root<Expression>> NewAssignExpression(
    Compilation& compilation, Identifier symbol,
    std::optional<gc::Root<Expression>> value) {
  if (value == std::nullopt) return std::nullopt;
  gc::Pool& pool = value->pool();
  if (std::optional<std::reference_wrapper<StackFrameHeader>> header =
          compilation.CurrentStackFrameHeader();
      header.has_value())
    if (std::optional<std::pair<size_t, Type>> argument_data =
            header->get().Find(symbol);
        argument_data.has_value()) {
      if (value.value()->SupportsType(argument_data->second)) {
        return pool.NewRoot(MakeNonNullUnique<StackFrameAssign>(
            argument_data->first,
            NewDelegatingExpression(std::move(value).value())));
      } else {
        compilation.AddError(Error{
            LazyString{L"Unable to assign a value to an argument of type "} +
            ToQuotedSingleLine(argument_data->second) +
            LazyString{L". Type found: "} +
            TypesToString(value.value()->Types())});
        return std::nullopt;
      }
    }

  static const vm::Namespace kEmptyNamespace;
  std::vector<Environment::LookupResult> variables =
      compilation.environment.ptr()->PolyLookup(kEmptyNamespace, symbol);
  if (variables.empty()) {
    compilation.AddError(Error{LazyString{L"Variable not found: \""} +
                               ToLazyString(symbol) + LazyString{L"\""}});
    return std::nullopt;
  }

  return VisitOptional(
      [&pool, &value, &symbol](const Environment::LookupResult& lookup_result)
          -> std::optional<gc::Root<Expression>> {
        return pool.NewRoot(MakeNonNullUnique<AssignExpression>(
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
            NewDelegatingExpression(std::move(value).value())));
      },
      [&] -> std::optional<gc::Root<Expression>> {
        compilation.AddError(Error{
            LazyString{L"Unable to assign a value to a variable supporting "
                       L"types: \""} +
            TypesToString(value.value()->Types()) +
            LazyString{L"\". Value types: "} +
            TypesToString(container::MaterializeVector(
                std::move(variables) |
                std::views::transform(&Environment::LookupResult::type)))});
        return std::nullopt;
      },
      container::FindFirstIf(
          variables, [&value](const Environment::LookupResult& lookup_result) {
            return value.value()->SupportsType(lookup_result.type);
          }));
}
}  // namespace afc::vm
