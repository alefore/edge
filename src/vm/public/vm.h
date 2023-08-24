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
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/language/error/value_or_error.h"
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

class Expression;
struct EvaluationOutput;

class Trampoline {
 public:
  struct Options {
    language::gc::Pool& pool;
    language::gc::Root<Environment> environment;
    std::function<void(std::function<void()>)> yield_callback;
  };

  Trampoline(Options options);

  void SetEnvironment(language::gc::Root<Environment> environment);
  const language::gc::Root<Environment>& environment() const;

  // Expression can be deleted as soon as this returns (even before a value is
  // given to the returned future).
  //
  // The Trampoline itself must not be deleted before the future is given a
  // value.
  futures::ValueOrError<EvaluationOutput> Bounce(Expression& expression,
                                                 Type expression_type);

  language::gc::Pool& pool() const;

 private:
  // We keep it by pointer (rather than by ref) to enable the assignment
  // operator.
  language::NonNull<language::gc::Pool*> pool_;
  std::list<std::wstring> namespace_;
  language::gc::Root<Environment> environment_;

  std::function<void(std::function<void()>)> yield_callback_;
  size_t jumps_ = 0;
};

class Expression {
 public:
  virtual ~Expression() = default;
  virtual std::vector<Type> Types() = 0;
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
  virtual std::unordered_set<Type> ReturnTypes() const = 0;

  bool SupportsType(const Type& type);

  bool IsBool() { return SupportsType(types::Bool{}); }
  bool IsInt() { return SupportsType(types::Int{}); };
  bool IsDouble() { return SupportsType(types::Double{}); };
  bool IsString() { return SupportsType(types::String{}); };

  virtual PurityType purity() = 0;

  // Returns a new copy of this expression.
  virtual language::NonNull<std::unique_ptr<Expression>> Clone() = 0;

  // The expression may be deleted as soon as `Evaluate` returns, even before
  // the returned Value has been given a value.
  //
  // The trampoline must not be deleted until the returned future is given a
  // value.
  virtual futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const Type& type) = 0;
};

struct EvaluationOutput {
  enum class OutputType { kReturn, kContinue };

  static EvaluationOutput New(language::gc::Root<Value> value) {
    return EvaluationOutput{.value = std::move(value)};
  }

  static EvaluationOutput Return(language::gc::Root<Value> value) {
    return EvaluationOutput{.value = std::move(value),
                            .type = OutputType::kReturn};
  }

  language::gc::Root<Value> value;
  OutputType type = OutputType::kContinue;
};

// Combine the return types of two sub-expressions (see Expression::ReturnType).
// If there's an error, a string will be stored in `error` describing it.
language::ValueOrError<std::unordered_set<Type>> CombineReturnTypes(
    std::unordered_set<Type> a, std::unordered_set<Type> b);

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
CompileFile(infrastructure::Path path, language::gc::Pool& pool,
            language::gc::Root<Environment> environment);

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
CompileString(const std::wstring& str, language::gc::Pool& pool,
              language::gc::Root<Environment> environment);

// `yield_callback` is an optional function that must ensure that the callback
// it receives will run in the future.
//
// `expr` can be deleted as soon as this returns (even before a value is given
// to the returned future).
futures::ValueOrError<language::gc::Root<Value>> Evaluate(
    Expression& expr, language::gc::Pool& pool,
    language::gc::Root<Environment> environment,
    std::function<void(std::function<void()>)> yield_callback);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
