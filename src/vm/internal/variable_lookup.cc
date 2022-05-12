#include "variable_lookup.h"

#include <glog/logging.h>

#include <unordered_set>

#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc::vm {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::VisitPointer;

class VariableLookup : public Expression {
 public:
  VariableLookup(Environment::Namespace symbol_namespace, std::wstring symbol,
                 std::vector<VMType> types)
      : symbol_namespace_(std::move(symbol_namespace)),
        symbol_(std::move(symbol)),
        types_(types) {}

  std::vector<VMType> Types() override { return types_; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType::kPure; }

  futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const VMType& type) override {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    return futures::Past(VisitPointer(
        trampoline.environment().value()->Lookup(
            trampoline.pool(), symbol_namespace_, symbol_, type),
        [](NonNull<std::unique_ptr<Value>> value) {
          DVLOG(5) << "Variable lookup: " << *value;
          return Success(EvaluationOutput::New(std::move(value)));
        },
        [this]() {
          return Error(L"Unexpected: variable value is null: " + symbol_);
        }));
  }

  NonNull<std::unique_ptr<Expression>> Clone() override {
    return MakeNonNullUnique<VariableLookup>(symbol_namespace_, symbol_,
                                             types_);
  }

 private:
  const Environment::Namespace symbol_namespace_;
  const std::wstring symbol_;
  const std::vector<VMType> types_;
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation* compilation,
                                              std::list<std::wstring> symbols) {
  CHECK(!symbols.empty());

  std::vector<NonNull<Value*>> result;

  auto symbol = std::move(symbols.back());
  symbols.pop_back();
  Environment::Namespace symbol_namespace(symbols.begin(), symbols.end());

  // We don't need to switch namespaces (i.e., we can use
  // `compilation->environment` directly) because during compilation, we know
  // that we'll be in the right environment.
  compilation->environment.value()->PolyLookup(symbol_namespace, symbol,
                                               &result);
  if (result.empty()) {
    compilation->AddError(L"Variable not found: `" + symbol + L"`");
    return nullptr;
  }
  std::vector<VMType> types;
  std::unordered_set<VMType> types_already_seen;

  for (auto& v : result) {
    if (types_already_seen.insert(v->type).second) {
      types.push_back(v->type);
    }
  }
  return std::make_unique<VariableLookup>(std::move(symbol_namespace),
                                          std::move(symbol), types);
}

}  // namespace afc::vm
