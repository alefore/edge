#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <glog/logging.h>

#include <optional>
#include <string>
#include <variant>

#include "src/language/ghost_type.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::language {
GHOST_TYPE(Error, std::wstring);

// Example: AugmentError(L"🖫 Save failed", error)
Error AugmentError(std::wstring prefix, Error error);

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator);

template <typename T>
using ValueOrError = std::variant<T, Error>;

template <typename T>
bool IsError(const ValueOrError<T>& value) {
  return std::holds_alternative<Error>(value);
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const ValueOrError<T>& p) {
  std::visit(
      overload{[&](const Error& error) { os << error; },
               [&](const T& value) { os << "[Value: " << value << "]"; }},
      p);
  return os;
}

template <typename>
struct IsValueOrError : std::false_type {};

template <typename T>
struct IsValueOrError<ValueOrError<T>> : std::true_type {};

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
ValueOrError<T> AugmentErrors(std::wstring prefix, ValueOrError<T> input) {
  std::visit(
      overload{[&](Error& error) { error = AugmentError(prefix, error); },
               [](T&) {}},
      input);
  return input;
}

#define ASSIGN_OR_RETURN(variable, expression)               \
  variable = ({                                              \
    auto tmp = expression;                                   \
    if (std::holds_alternative<afc::language::Error>(tmp))   \
      return std::get<afc::language::Error>(std::move(tmp)); \
    std::get<0>(std::move(tmp));                             \
  })

struct IgnoreErrors {
  void operator()(Error);
};

template <typename T>
T ValueOrDie(ValueOrError<T> value, std::wstring error_location = L"") {
  return std::visit(language::overload{
                        [&](Error error) {
                          LOG(FATAL) << error_location << ": " << error;
                          return std::optional<T>();
                        },
                        [](T t) { return std::optional<T>(std::move(t)); }},
                    std::move(value))
      .value();
}

template <typename Overload>
auto VisitCallback(Overload overload) {
  return
      [overload](auto value) { return std::visit(overload, std::move(value)); };
}

template <typename T>
std::unique_ptr<T> ToUniquePtr(
    ValueOrError<NonNull<std::unique_ptr<T>>> value_or_error) {
  return std::visit(overload{[](Error) { return std::unique_ptr<T>(); },
                             [](NonNull<std::unique_ptr<T>> value) {
                               return std::move(value.get_unique());
                             }},
                    std::move(value_or_error));
}

template <typename T>
std::optional<T> OptionalFrom(ValueOrError<T> value) {
  return std::visit(
      overload{[](Error) { return std::optional<T>(); },
               [](T t) { return std::optional<T>(std::move(t)); }},
      std::move(value));
}

}  // namespace afc::language
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
