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

class Trampoline {
 public:
  // A consumer of a value receives the value and an ongoing evaluation.
  //
  // We allow "trampoline" style execution. Suppose we need to evaluate
  // F(G(value)). This means we'd feed the value to G' (a continuation bound to)
  // G, which would already know that its continuation is F'. Without
  // OngoingEvaluation, this means we could have a long chain of recursive
  // calls. Instead, G' receives the value, transforms it (applies G), and then
  // just adjusts `consumer` and `expression_for_trampoline` in its
  // continuation.
  //
  // G may be an asynchronous transformation. In this case, G' will start its
  // execution and return (leaving `expression_for_trampoline` as nullptr and
  // thus effectively jumping out of the trampoline). When G(value) becomes
  // available, G' will need to create a new trampoline. Thus when G' resumes
  // F', the Trampoline may be different than when F' was created by G'. And
  // this is the reason the Continuation receives the Trampoline.
  using Continuation = std::function<void(std::unique_ptr<Value>, Trampoline*)>;

  Trampoline(Environment* environment, Continuation return_continuation);

  void Enter(Expression* expression);

  // Saves the state (continuations ane environment) of the current trampoline
  // and returns a callback that can be used to restore it into a trampoline.
  std::function<void(Trampoline*)> Save();

  void SetEnvironment(Environment* environment);
  Environment* environment() const;

  void SetReturnContinuation(Continuation continuation);
  Continuation return_continuation() const;

  void SetContinuation(Continuation continuation);

  // Returns the function that can resume. Roughly equivalent to Continue, but
  // allows us to return and resume continuation later.
  std::function<void(std::unique_ptr<Value>)> Interrupt();

  void Bounce(Expression* new_expression, Continuation new_continuation);
  void Return(std::unique_ptr<Value> value);
  void Continue(std::unique_ptr<Value> value);

 private:
  Environment* environment_ = nullptr;

  Continuation return_continuation_;
  Continuation continuation_;

  // Set by Bounce (and Enter), read by Enter.
  Expression* expression_ = nullptr;
};

class Expression {
 public:
  virtual ~Expression() {}
  virtual const VMType& type() = 0;

  // Returns a new copy of this expression.
  virtual std::unique_ptr<Expression> Clone() = 0;

  // Implementation details, not relevant for customers.
  // TODO: Figure out a nice way to hide this.

  // Must arrange for either Trampoline::Return, Trampoline::Continue, or
  // Trampoline::Bounce to be called (whether before or after returning).
  virtual void Evaluate(Trampoline* evaluation) = 0;
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
