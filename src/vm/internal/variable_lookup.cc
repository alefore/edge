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
  
class VariableLookup : public Expression {
 public:
  VariableLookup(const string& symbol, const VMType& type)
      : symbol_(symbol), type_(type) {}

  const VMType& type() {
    return type_;
  }

  void Evaluate(OngoingEvaluation* evaluation) {
    DVLOG(5) << "Look up symbol: " << symbol_;
    Value* result = evaluation->environment->Lookup(symbol_);
    assert(result != nullptr);
    evaluation->value = unique_ptr<Value>(new Value(*result));
  }

 private:
  const string symbol_;
  const VMType type_;
};

}  // namespace

unique_ptr<Expression> NewVariableLookup(
    Compilation* compilation, const string& symbol) {
  Value* result = compilation->environment->Lookup(symbol);
  if (result == nullptr) {
    compilation->AddError("Variable not found: \"" + symbol + "\"");
    return nullptr;
  }
  return unique_ptr<Expression>(new VariableLookup(symbol, result->type));
}

}  // namespace
}  // namespace afc
