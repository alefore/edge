#include "variable_lookup.h"

#include <glog/logging.h>

#include <unordered_set>

#include "compilation.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::VisitPointer;

namespace afc::vm {
namespace {

class VariableLookup : public Expression {
 public:
  VariableLookup(Namespace symbol_namespace, std::wstring symbol,
                 std::vector<Type> types)
      : symbol_namespace_(std::move(symbol_namespace)),
        symbol_(std::move(symbol)),
        types_(types) {}

  std::vector<Type> Types() override { return types_; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    return futures::Past(VisitPointer(
        trampoline.environment().ptr()->Lookup(
            trampoline.pool(), symbol_namespace_, symbol_, type),
        [](gc::Root<Value> value) {
          DVLOG(5) << "Variable lookup: " << value.ptr().value();
          return Success(EvaluationOutput::New(std::move(value)));
        },
        [this]() {
          return Error(L"Unexpected: variable value is null: " + symbol_);
        }));
  }

 private:
  const Namespace symbol_namespace_;
  const std::wstring symbol_;
  const std::vector<Type> types_;
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation* compilation,
                                              std::list<std::wstring> symbols) {
  CHECK(!symbols.empty());

  auto symbol = std::move(symbols.back());
  symbols.pop_back();
  Namespace symbol_namespace(
      std::vector<std::wstring>(symbols.begin(), symbols.end()));

  // We don't need to switch namespaces (i.e., we can use
  // `compilation->environment` directly) because during compilation, we know
  // that we'll be in the right environment.
  std::vector<gc::Root<Value>> result;
  compilation->environment.ptr()->PolyLookup(symbol_namespace, symbol, &result);
  if (result.empty()) {
    compilation->AddError(Error(L"Unknown variable: `" + symbol + L"`"));
    return nullptr;
  }
  std::unordered_set<Type> already_seen;
  return std::make_unique<VariableLookup>(
      std::move(symbol_namespace), std::move(symbol),
      container::MaterializeVector(
          std::move(result) | gc::view::Value |
          std::views::transform(&Value::type) |
          std::views::filter([&already_seen](const Type& type) {
            return already_seen.insert(type).second;
          })));
}

}  // namespace afc::vm
