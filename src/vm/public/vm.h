#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <functional>
#include <list>
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
struct EvaluationOutput;

class Trampoline {
 public:
  struct Options {
    std::shared_ptr<Environment> environment;
    std::function<void(std::function<void()>)> yield_callback;
  };

  Trampoline(Options options);

  void SetEnvironment(std::shared_ptr<Environment> environment);
  const std::shared_ptr<Environment>& environment() const;

  // Must ensure expression lives until the future is notified.
  futures::Value<EvaluationOutput> Bounce(Expression* expression,
                                          VMType expression_type);

 private:
  std::list<std::wstring> namespace_;
  std::shared_ptr<Environment> environment_;

  std::function<void(std::function<void()>)> yield_callback_;
  size_t jumps_ = 0;
};

class Expression {
 public:
  virtual ~Expression() = default;
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

  bool SupportsType(const VMType& type);

  bool IsBool() { return SupportsType(VMType::Bool()); }
  bool IsInteger() { return SupportsType(VMType::Integer()); };
  bool IsDouble() { return SupportsType(VMType::Double()); };
  bool IsString() { return SupportsType(VMType::String()); };

  // Returns a new copy of this expression.
  virtual std::unique_ptr<Expression> Clone() = 0;

  // The expression may be deleted as soon as `Evaluate` returns, even before
  // the returned Value has been given a value.
  virtual futures::Value<EvaluationOutput> Evaluate(Trampoline* evaluation,
                                                    const VMType& type) = 0;
};

struct EvaluationOutput {
  enum class OutputType { kReturn, kContinue, kAbort };

  static EvaluationOutput New(std::unique_ptr<Value> value) {
    EvaluationOutput output;
    CHECK(value != nullptr);
    output.value = std::move(value);
    return output;
  }
  static EvaluationOutput Return(std::unique_ptr<Value> value) {
    EvaluationOutput output;
    CHECK(value != nullptr);
    output.value = std::move(value);
    output.type = OutputType::kReturn;
    return output;
  }
  static EvaluationOutput Abort(afc::editor::Error error) {
    EvaluationOutput output;
    output.error = std::move(error);
    output.type = OutputType::kAbort;
    return output;
  }

  std::unique_ptr<Value> value;
  std::optional<afc::editor::Error> error;
  OutputType type = OutputType::kContinue;
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
futures::Value<std::unique_ptr<Value>> Evaluate(
    Expression* expr, std::shared_ptr<Environment> environment,
    std::function<void(std::function<void()>)> yield_callback);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
