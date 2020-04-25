#include "variable_lookup.h"

#include <glog/logging.h>

#include <unordered_set>

#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"
#include "compilation.h"

namespace afc {
namespace vm {

namespace {

class VariableLookup : public Expression {
 public:
  VariableLookup(std::wstring symbol, std::vector<VMType> types)
      : symbol_(std::move(symbol)), types_(types) {}

  std::vector<VMType> Types() override { return types_; }
  std::unordered_set<VMType> ReturnTypes() const override { return {}; }

  futures::Value<EvaluationOutput> Evaluate(Trampoline* trampoline,
                                            const VMType& type) override {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    CHECK(trampoline != nullptr);
    CHECK(trampoline->environment() != nullptr);
    Value* result = trampoline->environment()->Lookup(symbol_, type);
    CHECK(result != nullptr);
    DVLOG(5) << "Variable lookup: " << *result;
    return futures::Past(
        EvaluationOutput::New(std::make_unique<Value>(*result)));
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<VariableLookup>(symbol_, types_);
  }

 private:
  const std::wstring symbol_;
  const std::vector<VMType> types_;
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation* compilation,
                                              std::wstring symbol) {
  std::vector<Value*> result;
  compilation->environment->PolyLookup(symbol, &result);
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
  return std::make_unique<VariableLookup>(std::move(symbol), types);
}

}  // namespace vm
}  // namespace afc
