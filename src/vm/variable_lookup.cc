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
  const Namespace symbol_namespace_;
  const Identifier symbol_;
  const std::vector<Type> types_;

 public:
  VariableLookup(Namespace symbol_namespace, Identifier symbol,
                 std::vector<Type> types)
      : symbol_namespace_(std::move(symbol_namespace)),
        symbol_(std::move(symbol)),
        types_(types) {}

  std::vector<Type> Types() override { return types_; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    TRACK_OPERATION(vm_VariableLookup_Evaluate);
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
          return Error(L"Unexpected: variable value is null: " +
                       symbol_.read());
        }));
  }
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation& compilation,
                                              std::list<Identifier> symbols) {
  CHECK(!symbols.empty());

  auto symbol = std::move(symbols.back());
  symbols.pop_back();
  Namespace symbol_namespace(
      std::vector<Identifier>(symbols.begin(), symbols.end()));

  // We don't need to switch namespaces (i.e., we can use
  // `compilation->environment` directly) because during compilation, we know
  // that we'll be in the right environment.
  std::vector<Environment::LookupResult> result =
      compilation.environment.ptr()->PolyLookup(symbol_namespace, symbol);
  if (result.empty()) {
    compilation.AddError(
        Error(L"Unknown variable: `" + to_wstring(symbol) + L"`"));
    return nullptr;
  }

  std::unordered_set<Type> already_seen;
  std::vector<Type> types;
  // We can't use `MaterializeVector` here: we need to ensure that the filtering
  // is done in an order-preserving way.
  for (const Type& type :
       std::move(result) |
           std::views::transform(&Environment::LookupResult::value) |
           gc::view::Value | std::views::transform(&Value::type))
    if (already_seen.insert(type).second) types.push_back(type);
  return std::make_unique<VariableLookup>(std::move(symbol_namespace),
                                          std::move(symbol), types);
}

}  // namespace afc::vm
