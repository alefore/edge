#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <glog/logging.h>

#include <optional>
#include <string>
#include <variant>

#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::language {
struct Error {
  Error(std::wstring description) : description(std::move(description)) {}
  static Error Augment(std::wstring prefix, Error error) {
    return Error(prefix + L": " + error.description);
  }

  std::wstring description;
};

std::ostream& operator<<(std::ostream& os, const Error& p);

template <typename T>
class ValueOrError {
 public:
  using ValueType = T;

  ValueOrError() : value_(T()) {}

  ValueOrError(Error error) : value_(std::move(error)) {}
  ValueOrError(T value) : value_(std::move(value)) {}

  template <typename Other>
  explicit ValueOrError(Other other)
      : value_(std::variant<T, Error>(std::move(other))) {}

  template <typename Other>
  ValueOrError(ValueOrError<Other> other)
      : value_(other.IsError()
                   ? std::variant<T, Error>(std::move(other.error()))
                   : std::variant<T, Error>(std::move(other.value()))) {}

  std::optional<T> AsOptional() const {
    return std::holds_alternative<Error>(value_) ? std::optional<T>()
                                                 : std::optional<T>(value());
  }

  bool IsError() const { return std::holds_alternative<Error>(value_); }

  Error error() const {
    CHECK(IsError());
    return Error(std::get<Error>(value_));
  }

  const T& value() const {
    CHECK(!IsError()) << "Attempted to get value of ValueOrError with error: "
                      << error();
    return std::get<T>(value_);
  }

  T& value() {
    CHECK(!IsError()) << "Attempted to get value of ValueOrError with error: "
                      << error();
    return std::get<T>(value_);
  }

  T& value_or(T other) { return IsError() ? other : value(); }

  template <typename Other>
  ValueOrError<T> operator=(ValueOrError<Other>&& value) {
    value_ = std::forward<Other>(value.value_);
    return *this;
  }

  ValueOrError<T> operator=(T&& value) {
    value_ = std::forward<T>(value);
    return *this;
  }

  template <typename Overload>
  decltype(std::declval<Overload>()(std::declval<T>())) Visit(
      Overload overload) {
    return std::visit(overload, value_);
  }

  std::variant<T, Error>& variant() { return value_; }
  const std::variant<T, Error>& variant() const { return value_; }

 private:
  template <typename Other>
  friend class ValueOrError;

  std::variant<T, Error> value_;
};

template <typename T>
bool IsError(const ValueOrError<T>& value) {
  return std::holds_alternative<Error>(value.variant());
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const ValueOrError<T>& p) {
  if (p.IsError()) {
    os << p.error.value();
  } else {
    os << "[Value: " << p.value.value() << "]";
  }
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
  return input.IsError() ? Error::Augment(prefix, std::move(input.error()))
                         : input;
}

#define ASSIGN_OR_RETURN(variable, expression) \
  variable = ({                                \
    auto tmp = expression;                     \
    if (tmp.IsError()) return tmp.error();     \
    std::move(tmp.value());                    \
  })

struct IgnoreErrors {
  void operator()(Error);
};

template <typename T>
T ValueOrDie(ValueOrError<T> value, std::wstring error_location) {
  return std::visit(language::overload{
                        [&](Error error) {
                          LOG(FATAL) << error_location << ": " << error;
                          return std::optional<T>();
                        },
                        [](T t) { return std::optional<T>(std::move(t)); }},
                    std::move(value.variant()))
      .value();
}

template <typename Overload>
auto VisitCallback(Overload overload) {
  return [overload](auto value) {
    return std::visit(overload, std::move(value.variant()));
  };
}

template <typename T>
std::unique_ptr<T> ToUniquePtr(
    ValueOrError<NonNull<std::unique_ptr<T>>> value) {
  return std::visit(overload{[](Error) { return std::unique_ptr<T>(); },
                             [](NonNull<std::unique_ptr<T>> value) {
                               return std::move(value.get_unique());
                             }},
                    std::move(value.variant()));
}

}  // namespace afc::language
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
