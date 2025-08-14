#ifndef __AFC_VM_EXPRESSION_H__
#define __AFC_VM_EXPRESSION_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "src/vm/stack.h"
#include "src/vm/types.h"

namespace afc::vm {
class Environment;
struct EvaluationOutput;
class Expression;

class Trampoline {
  struct ConstructorAccessTag {};

  // We keep it by pointer (rather than by ref) to enable the assignment
  // operator.
  std::list<std::wstring> namespace_;
  // Remove `environment_`. Expressions shouldn't be accessing the environment
  // at runtime but, instead, should hold pointers that they obtain during
  // compilation. Of course that requires us to significantly extend
  // Environment.
  language::gc::Ptr<Environment> environment_;
  language::gc::Ptr<Stack> stack_;

  std::function<void(language::OnceOnlyFunction<void()>)> yield_callback_;
  size_t jumps_ = 0;

 public:
  struct Options {
    language::gc::Ptr<Environment> environment;
    std::function<void(language::OnceOnlyFunction<void()>)> yield_callback;
  };

  static language::gc::Root<Trampoline> New(Options options);

  Trampoline(ConstructorAccessTag, Options options,
             language::gc::Ptr<Stack> stack);

  void SetEnvironment(language::gc::Ptr<Environment> environment);
  const language::gc::Ptr<Environment>& environment() const;

  Stack& stack();

  // Expression can be deleted as soon as this returns (even before a value is
  // given to the returned future).
  //
  // The Trampoline itself must not be deleted before the future is given a
  // value.
  futures::ValueOrError<EvaluationOutput> Bounce(
      const language::gc::Ptr<Expression>& expression, Type expression_type);

  language::gc::Pool& pool() const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
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
  bool IsNumber() { return SupportsType(types::Number{}); };
  bool IsString() { return SupportsType(types::String{}); };

  virtual PurityType purity() = 0;

  // The expression may be deleted as soon as `Evaluate` returns, even before
  // the returned Value has been given a value.
  //
  // The trampoline must not be deleted until the returned future is given a
  // value.
  virtual futures::ValueOrError<EvaluationOutput> Evaluate(
      Trampoline& trampoline, const Type& type) = 0;

  // Used by the garbage collector to find objects reachable from this one.
  // This should be overridden in subclasses that hold gc::Ptr<> or gc::Root<>
  // to return all such objects.
  virtual std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const = 0;
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

// `yield_callback` is an optional function that must ensure that the callback
// it receives will run in the future.
//
// `expr` can be deleted as soon as this returns (even before a value is given
// to the returned future).
futures::ValueOrError<language::gc::Root<Value>> Evaluate(
    const language::gc::Ptr<Expression>& expr,
    const language::gc::Ptr<Environment>& environment,
    std::function<void(language::OnceOnlyFunction<void()>)> yield_callback);

// If a value of `original` type can be promoted implicitly to a value of
// `desired` type, returns a function that executes the promotion.
using ImplicitPromotionCallback =
    std::function<language::gc::Root<Value>(language::gc::Root<Value>)>;
ImplicitPromotionCallback GetImplicitPromotion(Type original, Type desired);
}  // namespace afc::vm
#endif