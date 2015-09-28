#include "variable_lookup.h"

#include <cassert>

#include <glog/logging.h>

#include "compilation.h"
#include "evaluation.h"
#include "../public/environment.h"
#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

// TODO: Don't pass symbol by const reference.
class VariableLookup : public Expression {
 public:
  VariableLookup(const wstring& symbol, const VMType& type)
      : symbol_(symbol), type_(type) {}

  const VMType& type() {
    return type_;
  }

  void Evaluate(OngoingEvaluation* evaluation) {
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    Value* result = evaluation->environment->Lookup(symbol_);
    assert(result != nullptr);
    evaluation->value = unique_ptr<Value>(new Value(*result));
  }

 private:
  const wstring symbol_;
  const VMType type_;
};

}  // namespace

unique_ptr<Expression> NewVariableLookup(
    Compilation* compilation, const wstring& symbol) {
  Value* result = compilation->environment->Lookup(symbol);
  if (result == nullptr) {
    compilation->AddError(L"Variable not found: \"" + symbol + L"\"");
    return nullptr;
  }
  return unique_ptr<Expression>(new VariableLookup(symbol, result->type));
}

}  // namespace
}  // namespace afc
