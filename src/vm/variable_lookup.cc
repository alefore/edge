#include "variable_lookup.h"

#include <glog/logging.h>

#include <unordered_set>

#include "compilation.h"
#include "src/language/gc_view.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::VisitPointer;

namespace gc = language::gc;

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

  std::vector<gc::Root<Value>> result;

  auto symbol = std::move(symbols.back());
  symbols.pop_back();
  Namespace symbol_namespace(
      std::vector<std::wstring>(symbols.begin(), symbols.end()));

  // We don't need to switch namespaces (i.e., we can use
  // `compilation->environment` directly) because during compilation, we know
  // that we'll be in the right environment.
  compilation->environment.ptr()->PolyLookup(symbol_namespace, symbol, &result);
  if (result.empty()) {
    compilation->AddError(Error(L"Unknown variable: `" + symbol + L"`"));
    return nullptr;
  }
  std::vector<Type> types;
  std::unordered_set<Type> types_already_seen;

  for (const Value& v : RootValueView(result))
    if (types_already_seen.insert(v.type).second) types.push_back(v.type);
  return std::make_unique<VariableLookup>(std::move(symbol_namespace),
                                          std::move(symbol), types);
}

}  // namespace afc::vm
