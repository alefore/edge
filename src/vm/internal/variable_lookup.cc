#include "variable_lookup.h"

#include <unordered_set>

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

// TODO: Don't pass symbol by const reference.
class VariableLookup : public Expression {
 public:
  VariableLookup(const wstring& symbol, std::vector<VMType> types)
      : symbol_(symbol), types_(types) {}

  std::vector<VMType> Types() override { return types_; }

  void Evaluate(Trampoline* trampoline, const VMType& type) override {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    Value* result = trampoline->environment()->Lookup(symbol_, type);
    CHECK(result != nullptr);
    CHECK(trampoline != nullptr);
    DVLOG(5) << "Variable lookup: " << *result;
    trampoline->Continue(std::make_unique<Value>(*result));
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<VariableLookup>(symbol_, types_);
  }

 private:
  const wstring symbol_;
  const std::vector<VMType> types_;
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation* compilation,
                                              const wstring& symbol) {
  std::vector<Value*> result;
  compilation->environment->PolyLookup(symbol, &result);
  if (result.empty()) {
    compilation->AddError(L"Variable not found: \"" + symbol + L"\"");
    return nullptr;
  }
  std::unordered_set<VMType> types;
  for (auto& v : result) {
    types.insert(v->type);
  }
  return std::make_unique<VariableLookup>(
      symbol, std::vector<VMType>(types.begin(), types.end()));
}

}  // namespace vm
}  // namespace afc
