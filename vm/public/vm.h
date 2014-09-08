#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <map>
#include <memory>
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

class Environment;
class Evaluation;
class VMType;

template<typename A>
struct RecursiveHelper
{
  typedef function<pair<RecursiveHelper, A>(A)> type;
  RecursiveHelper(type f) : func(f) {}
  operator type() { return func; }
  type func;
};

class Expression {
 public:
  virtual ~Expression() {}
  virtual const VMType& type() = 0;

  // Implementation details, not relevant for customers.
  // TODO: Figure out a nice way to hide this.

  // We use a continuation trampoline: to evaluate an expression, we must pass
  // the continuation that wants to receive the value.  The expression will
  // return a pair with a continuation and a value, and the runner must feed the
  // value to the continuation and keep doing that on the returned value, until
  // it can verify that the original continuation has run.
  typedef RecursiveHelper<unique_ptr<Value>> Continuation;

  virtual pair<Continuation, unique_ptr<Value>> Evaluate(
      const Evaluation& evaluation) = 0;
};

unique_ptr<Expression> CompileFile(
    const string& path,
    Environment* environment,
    string* error_description);

unique_ptr<Expression> CompileString(
    const string& str,
    Environment* environment,
    string* error_description);

unique_ptr<Value> Evaluate(Expression* expr, Environment* environment);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
