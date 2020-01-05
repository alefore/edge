#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/futures/futures.h"
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
  // The continuation receives the current trampoline because it may change
  // during the execution of an expression.
  using Continuation = std::function<void(std::unique_ptr<Value>, Trampoline*)>;

  struct Options {
    std::shared_ptr<Environment> environment;
    Continuation return_continuation;
    std::function<void(std::function<void()>)> yield_callback;
  };

  Trampoline(Options options);

  // Must ensure expression lives until return_continuation is called.
  void Enter(Expression* expression);

  void SetEnvironment(std::shared_ptr<Environment> environment);
  const std::shared_ptr<Environment>& environment() const;

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
  std::shared_ptr<Environment> environment_;

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

  // Must call either Trampoline::Return, Trampoline::Continue,
  // Trampoline::Bounce, or Trampoline::Interrupt before returning.
  //
  // The caller must ensure that the expression doesn't get deleted before the
  // trampoline receives the value (i.e., either Trampoline::Return or
  // Trampoline::Continue).
  virtual void Evaluate(Trampoline* evaluation, const VMType& type) = 0;
};

// Combine the return types of two sub-expressions (see Expression::ReturnType).
// If there's an error, a string will be stored in `error` describing it.
std::optional<std::unordered_set<VMType>> CombineReturnTypes(
    std::unordered_set<VMType> a, std::unordered_set<VMType> b,
    std::wstring* error);

unique_ptr<Expression> CompileFile(const string& path,
                                   std::shared_ptr<Environment> environment,
                                   wstring* error_description);

unique_ptr<Expression> CompileString(const wstring& str,
                                     std::shared_ptr<Environment> environment,
                                     wstring* error_description);

// Caller must make sure expr lives until consumer runs. `yield_callback` is an
// optional function that must ensure that the callback it receives will run
// in the future.
futures::DelayedValue<std::unique_ptr<Value>> Evaluate(
    Expression* expr, std::shared_ptr<Environment> environment,
    std::function<void(std::function<void()>)> yield_callback);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
