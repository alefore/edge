#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
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
struct VMType;

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

  struct Options {
    Environment* environment = nullptr;
    Continuation return_continuation;
    std::function<void(std::function<void()>)> yield_callback;
  };

  Trampoline(Options options);

  // Must ensure expression lives until return_continuation is called.
  void Enter(Expression* expression);

  void SetEnvironment(Environment* environment);
  Environment* environment() const;

  void SetReturnContinuation(Continuation continuation);
  Continuation return_continuation() const;

  void SetContinuation(Continuation continuation);

  // Returns the function that can resume. Roughly equivalent to Continue, but
  // allows us to return and resume continuation later.
  std::function<void(std::unique_ptr<Value>)> Interrupt();

  // Must ensure new_expression lives until new_contination is called.
  void Bounce(Expression* new_expression, VMType type,
              Continuation new_continuation);
  void Return(std::unique_ptr<Value> value);
  void Continue(std::unique_ptr<Value> value);

 private:
  Environment* environment_ = nullptr;

  Continuation return_continuation_;
  Continuation continuation_;

  std::function<void(std::function<void()>)> yield_callback_;
  size_t jumps_ = 0;

  // Set by Bounce (and Enter), read by Enter.
  Expression* expression_ = nullptr;
  VMType desired_type_;
};

class Expression {
 public:
  virtual ~Expression() {}
  virtual std::vector<VMType> Types() = 0;
  // If the expression can cause a `return` statement to be evaluated, this
  // should return the type. Most expressions will return an empty set.
  // Expressions that combine sub-expressions should use `CombineReturnTypes`.
  //
  // This is a container (rather than a single value) because the expression
  // could ambiguously refer to a function that has multiple (polymorphic)
  // definitions, as in:
  //
  //   void Foo();
  //   void Foo(int);
  //   X GetFoo() { return Foo; }
  //
  // In this case, the evaluation of the body of `GetFoo` will reflect that the
  // expression could return multiple values (and, depending on the type `X`,
  // one will be selected).
  virtual std::unordered_set<VMType> ReturnTypes() const = 0;

  bool SupportsType(const VMType& type) {
    auto types = Types();
    return std::find(types.begin(), types.end(), type) != types.end();
  }

  bool IsBool() { return SupportsType(VMType::Bool()); }
  bool IsInteger() { return SupportsType(VMType::Integer()); };
  bool IsDouble() { return SupportsType(VMType::Double()); };
  bool IsString() { return SupportsType(VMType::String()); };

  // Returns a new copy of this expression.
  virtual std::unique_ptr<Expression> Clone() = 0;

  // Implementation details, not relevant for customers.
  // TODO: Figure out a nice way to hide this.

  // Must arrange for either Trampoline::Return, Trampoline::Continue, or
  // Trampoline::Bounce to be called (whether before or after returning).
  virtual void Evaluate(Trampoline* evaluation, const VMType& type) = 0;
};

// Combine the return types of two sub-expressions (see Expression::ReturnType).
// If there's an error, a string will be stored in `error` describing it.
std::optional<std::unordered_set<VMType>> CombineReturnTypes(
    std::unordered_set<VMType> a, std::unordered_set<VMType> b,
    std::wstring* error);

unique_ptr<Expression> CompileFile(const string& path, Environment* environment,
                                   wstring* error_description);

unique_ptr<Expression> CompileString(const wstring& str,
                                     Environment* environment,
                                     wstring* error_description);

// Caller must make sure expr lives until consumer runs. `yield_callback` is an
// optional function that must ensure that the callback it receives will run
// in the future.
void Evaluate(Expression* expr, Environment* environment,
              std::function<void(std::unique_ptr<Value>)> consumer,
              std::function<void(std::function<void()>)> yield_callback);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
