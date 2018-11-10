#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <map>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "types.h"

namespace afc {
namespace vm {

using std::function;
using std::map;
using std::pair;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using std::wstring;

class Environment;
class Evaluation;
class VMType;

class Expression;

struct OngoingEvaluation {
  Environment* environment = nullptr;

  // The current value, intended to be consumed by the advancer.
  std::function<void(std::unique_ptr<Value>)> consumer;
  std::function<void(std::unique_ptr<Value>)> return_consumer;

  Expression* expression_for_trampoline = nullptr;
};

void EvaluateExpression(
    OngoingEvaluation* evaluation,
    Expression* expression,
    std::function<void(std::unique_ptr<Value>)> consumer);

class Expression {
 public:
  virtual ~Expression() {}
  virtual const VMType& type() = 0;

  // Implementation details, not relevant for customers.
  // TODO: Figure out a nice way to hide this.

  // Should adjust OngoingEvaluation to evaluate the current expression and
  // ensure that eventually (after advancing the evaluation) the value of the
  // expression will be received by the current advancer.
  virtual void Evaluate(OngoingEvaluation* evaluation) = 0;
};

unique_ptr<Expression> CompileFile(
    const string& path,
    Environment* environment,
    wstring* error_description);

unique_ptr<Expression> CompileString(
    const wstring& str,
    Environment* environment,
    wstring* error_description);

unique_ptr<Expression> CompileString(
    const wstring& str,
    Environment* environment,
    wstring* error_description,
    const VMType& return_type);

void Evaluate(
    Expression* expr,
    Environment* environment,
    std::function<void(std::unique_ptr<Value>)> consumer);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
