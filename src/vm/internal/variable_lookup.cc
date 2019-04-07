#include "variable_lookup.h"

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
  VariableLookup(const wstring& symbol, const VMType& type)
      : symbol_(symbol), type_(type) {}

  const VMType& type() { return type_; }

  void Evaluate(Trampoline* trampoline) {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    Value* result = trampoline->environment()->Lookup(symbol_, type_);
    CHECK(result != nullptr);
    CHECK(trampoline != nullptr);
    DVLOG(5) << "Variable lookup: " << *result;
    trampoline->Continue(std::make_unique<Value>(*result));
  }

  std::unique_ptr<Expression> Clone() override {
    return std::make_unique<VariableLookup>(symbol_, type_);
  }

 private:
  const wstring symbol_;
  const VMType type_;
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
  return std::make_unique<VariableLookup>(symbol, result[0]->type);
}

}  // namespace vm
}  // namespace afc
