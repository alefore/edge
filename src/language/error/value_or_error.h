#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <glog/logging.h>

#include <expected>
#include <optional>
#include <string>
#include <variant>

#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::language {
class Error : public GhostType<Error, language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

// Example: AugmentError(L"🖫 Save failed", error)
Error AugmentError(language::lazy_string::LazyString prefix, Error error);

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator);

#define USE_EXPECTED 0

#if USE_EXPECTED
// New implementation
template <typename T>
using ValueOrError = std::expected<T, Error>;

template <typename T>
struct ValueOrErrorTraits;

template <typename T>
struct ValueOrErrorTraits<ValueOrError<T>> {
  using value_type = T;
};

template <typename>
struct IsValueOrError : std::false_type {};

template <typename T>
struct IsValueOrError<ValueOrError<T>> : std::true_type {};

template <typename E>
auto MakeUnexpected(E&& e) {
  return std::unexpected(Error(std::forward<E>(e)));
}

template <typename T>
bool HasValue(const ValueOrError<T>& value) {
  return value.has_value();
}

template <typename T, typename V = std::remove_cvref_t<T>>
  requires std::is_same_v<V, ValueOrError<typename V::value_type>>
auto GetError(T&& e) {
  return e.error();
}

template <typename T, typename OnSuccess, typename OnError>
auto Visit(const ValueOrError<T>& v, OnSuccess&& s, OnError&& e) {
  if (v)
    return s(*v);
  else
    return e(v.error());
}
#else
// Old
template <typename T>
using ValueOrError = std::variant<T, Error>;

template <typename T>
struct ValueOrErrorTraits;

template <typename T>
struct ValueOrErrorTraits<ValueOrError<T>> {
  using value_type = T;
};

template <typename>
struct IsValueOrError : std::false_type {};

template <typename T>
struct IsValueOrError<ValueOrError<T>> : std::true_type {};

template <typename E>
auto MakeUnexpected(E&& e) {
  return Error(std::forward<E>(e));
}

template <typename T>
bool HasValue(const ValueOrError<T>& value) {
  return !std::holds_alternative<Error>(value);
}

template <typename T, typename V = std::remove_cvref_t<T>>
  requires std::is_same_v<V, ValueOrError<std::variant_alternative_t<0, V>>>
auto GetError(T&& e) {
  return std::get<Error>(std::forward<T>(e));
}

template <typename V, typename OnSuccess, typename OnError,
          typename = std::enable_if_t<IsValueOrError<std::decay_t<V>>::value>>
auto Visit(V&& v, OnSuccess&& s, OnError&& e) {
  return std::visit(
      overload{std::forward<OnSuccess>(s), std::forward<OnError>(e)},
      std::forward<V>(v));
}

#endif

template <typename T>
bool IsError(const T&) {
  return false;
}

template <typename T>
bool IsError(const ValueOrError<T>& value) {
  return !HasValue(value);
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const ValueOrError<T>& p) {
  std::visit(overload{[&](const Error& error) { os << error; },
                      [&](const T& value) {
                        os << "[ValueOrError::Value: " << value << "]";
                      }},
             p);
  return os;
}

struct EmptyValue {};
using PossibleError = ValueOrError<EmptyValue>;

// TODO: It'd be nicer to return `ValueType<EmptyValue>` and let that be
// implicitly converted to ValueOrError. However, `futures::Past(Success())`
// would cease to work, since `futures::Value` can't yet implicitly convert
// types.
ValueOrError<EmptyValue> Success();

template <typename T>
ValueOrError<T> Success(T t) {
  return ValueOrError<T>(std::move(t));
}

template <typename T>
ValueOrError<T> AugmentError(language::lazy_string::LazyString prefix,
                             ValueOrError<T> input) {
  std::visit(
      overload{[&](Error& error) { error = AugmentError(prefix, error); },
               [](T&) {}},
      input);
  return input;
}

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#define RETURN_IF_ERROR(expr)                                      \
  if (auto CONCAT(return_if_error_result, __LINE__) = expr;        \
      language::IsError(CONCAT(return_if_error_result, __LINE__))) \
  return language::MakeUnexpected(                                 \
      GetError(CONCAT(return_if_error_result, __LINE__)))

#define DECLARE_OR_RETURN(variable, expr)                                     \
  decltype(auto) CONCAT(tmp_result_, __LINE__) = expr;                        \
  if (IsError(CONCAT(tmp_result_, __LINE__)))                                 \
    return language::MakeUnexpected(GetError(CONCAT(tmp_result_, __LINE__))); \
  variable = language::ValueOrDie(std::move(CONCAT(tmp_result_, __LINE__)));

#define ASSIGN_OR_RETURN(variable, expression)                   \
  variable = ({                                                  \
    auto tmp = expression;                                       \
    if (IsError(tmp))                                            \
      return language::MakeUnexpected(GetError(std::move(tmp))); \
    language::ValueOrDie(std::move(tmp));                        \
  })

struct IgnoreErrors {
  void operator()(Error);
};

#define VALUE_OR_DIE(value_expr)                         \
  afc::language::ValueOrDie(                             \
      value_expr,                                        \
      afc::language::lazy_string::LazyString{            \
          afc::language::FromByteString(__FILE__)} +     \
          afc::language::lazy_string::LazyString{L":"} + \
          afc::language::lazy_string::LazyString{std::to_wstring(__LINE__)})

template <typename V>
auto ValueOrDie(V&& value, language::lazy_string::LazyString error_location) {
#if USE_EXPECTED
  if (IsError(value)) {
    LOG(FATAL) << error_location << ": " << value.error();
    throw std::runtime_error("Error in ValueOrDie.");
  }
  return std::forward<V>(value).value();
#else
  using T = ValueOrErrorTraits<std::decay_t<V>>::value_type;
  return std::visit(
      language::overload{[&](const Error& error) -> T {
                           LOG(FATAL) << error_location << ": " << error;
                           throw std::runtime_error("Error in ValueOrDie.");
                         },
                         []<typename U>(U&& t) -> T
                           requires(!std::is_same_v<std::decay_t<U>, Error>)
                         { return std::forward<decltype(t)>(t); }},
                         std::forward<V>(value));
#endif
}

template <typename V>
auto ValueOrDie(V&& value) {
  return ValueOrDie(std::forward<V>(value),
                    language::lazy_string::LazyString{});
}

template <typename T>
T ValueOrDie(ValueOrError<T>&& value, std::wstring error_location) {
  return ValueOrDie(std::forward<ValueOrError<T>>(value),
                    language::lazy_string::LazyString{error_location});
}

template <typename Overload>
auto VisitCallback(Overload overload) {
  return
      [overload](auto value) { return std::visit(overload, std::move(value)); };
}

template <typename T>
std::optional<T> OptionalFrom(ValueOrError<T> value) {
  return Visit(
      std::move(value), [](T t) { return std::optional<T>(std::move(t)); },
      [](Error) { return std::optional<T>(); });
}

namespace error {
template <typename T>
ValueOrError<T> FromOptional(std::optional<T> value) {
  if (value.has_value()) return value.value();
  return Error{language::lazy_string::LazyString{L"No value."}};
}

}  // namespace error
}  // namespace afc::language
// Monadic operator+.
template <typename A>
afc::language::ValueOrError<A> operator+(afc::language::ValueOrError<A> x,
                                         afc::language::ValueOrError<A> y) {
  if (afc::language::IsError(x)) return std::get<afc::language::Error>(x);
  if (afc::language::IsError(y)) return std::get<afc::language::Error>(y);
  return afc::language::ValueOrDie(std::move(x)) +
         afc::language::ValueOrDie(std::move(y));
}
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
