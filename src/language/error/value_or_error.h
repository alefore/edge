#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <glog/logging.h>

#include <expected>
#include <optional>
#include <string>
#include <variant>

#include "src/language/error/base.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::language {

class Error : public GhostType<Error, language::lazy_string::LazyString> {
  using GhostType::GhostType;

 public:
  operator std::unexpected<Error>() const {
    return std::unexpected<Error>(*this);
  }

  template <typename T>
  operator std::expected<T, Error>() const {
    return std::unexpected<Error>(*this);
  }
};

// Example: AugmentError(L"🖫 Save failed", error)
Error AugmentError(language::lazy_string::LazyString prefix, Error error);

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator);

template <typename T>
ValueOrError<T> CaptureErrors(ValueOrError<T> input,
                              std::vector<Error>& output) {
  Visit(
      input, [](const T&) {},
      [&output](const Error& error) { output.push_back(error); });
  return input;
}

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

template <typename T>
struct is_expected : std::false_type {};

template <typename T, typename E>
struct is_expected<std::expected<T, E>> : std::true_type {};

template <typename T>
concept ExpectedType = is_expected<std::remove_cvref_t<T>>::value;

template <ExpectedType V, typename OnSuccess, typename OnError>
auto Visit(V&& v, OnSuccess&& s, OnError&& e) {
  if (v)
    return std::forward<OnSuccess>(s)(std::forward<V>(v).value());
  else
    return std::forward<OnError>(e)(std::forward<V>(v).error());
}

template <typename T>
inline constexpr bool IsValueOrError_v = IsValueOrError<T>::value;

template <typename T>
std::ostream& operator<<(std::ostream& os, const ValueOrError<T>& p) {
  Visit(
      p,
      [&](const T& value) { os << "[ValueOrError::Value: " << value << "]"; },
      [&](const Error& error) { os << error; });
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
  return input.transform_error([&prefix](Error error) {
    return AugmentError(std::move(prefix), std::move(error));
  });
}

struct IgnoreErrors {
  void operator()(Error);
};

template <typename V, typename OnSuccess,
          typename = std::enable_if_t<IsValueOrError<std::decay_t<V>>::value>>
auto VisitValue(V&& v, OnSuccess&& s) {
  return Visit(std::forward<V>(v), std::forward<OnSuccess>(s), IgnoreErrors{});
}

#define VALUE_OR_DIE(value_expr)                         \
  afc::language::ValueOrDie(                             \
      value_expr,                                        \
      afc::language::lazy_string::LazyString{            \
          afc::language::FromByteString(__FILE__)} +     \
          afc::language::lazy_string::LazyString{L":"} + \
          afc::language::lazy_string::LazyString{std::to_wstring(__LINE__)})

template <typename V>
auto ValueOrDie(V&& value, language::lazy_string::LazyString error_location) {
  if (!value) {
    LOG(FATAL) << error_location << ": " << value.error();
    throw std::runtime_error("Error in ValueOrDie.");
  }
  return std::forward<V>(value).value();
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
  RETURN_IF_ERROR(x);
  RETURN_IF_ERROR(y);
  return std::move(x).value() + std::move(y).value();
}
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
