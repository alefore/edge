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

struct OngoingEvaluation {
  // The function to run to advance the evaluation.
  //
  // When this is run, the advancer field in the passed instance will be used
  // both as an input and output paremeter: as an output parameter, the advancer
  // sets the next piece of code that should run; as an input parameter, the
  // advancer can signal that the computation is done (by just leaving it
  // unmodified).
  std::function<void(OngoingEvaluation*)> advancer;

  // The function to run to halt the evaluation and return a value to the parent
  // scope.
  std::function<void(OngoingEvaluation*)> return_advancer;

  Environment* environment;

  // The current value, intended to be consumed by the advancer.
  unique_ptr<Value> value;
};

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

unique_ptr<Value> Evaluate(Expression* expr, Environment* environment);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
