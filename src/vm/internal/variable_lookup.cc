#include "variable_lookup.h"

#include <cassert>

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

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    unique_ptr<Value> output = Value::NewVoid();
    Value* result = evaluation.environment->Lookup(symbol_);
    assert(result != nullptr);
    *output = *result;
    return make_pair(evaluation.continuation, std::move(output));
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
    compilation->errors.push_back("Variable not found: \"" + symbol + "\"");
    return nullptr;
  }
  return unique_ptr<Expression>(new VariableLookup(symbol, result->type));
}

}  // namespace
}  // namespace afc
