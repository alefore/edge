#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <glog/logging.h>

#include <optional>
#include <string>

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
struct ValueType {
  ValueType(T value) : value(std::move(value)) {}
  T value;
};

template <typename T>
class ValueOrError {
 public:
  using ValueType = T;

  ValueOrError() : value_() {}

  ValueOrError(Error error) : error_(std::move(error)) {}
  ValueOrError(language::ValueType<T> value) : value_(std::move(value.value)) {}

  template <typename Other>
  ValueOrError(ValueOrError<Other> other)
      : value_(other.IsError() ? std::optional<T>() : std::move(other.value())),
        error_(other.IsError() ? std::move(other.error())
                               : std::optional<Error>()) {}

  std::optional<T> AsOptional() const {
    return IsError() ? std::optional<T>() : value();
  }

  bool IsError() const { return error_.has_value(); }

  Error error() const {
    CHECK(IsError());
    return Error(error_.value());
  }

  const T& value() const {
    CHECK(!IsError());
    return value_.value();
  }

  T& value() {
    CHECK(!IsError());
    return value_.value();
  }

  T& value_or(T other) { return IsError() ? other : value(); }

 private:
  // Exactly one of these should have a value.
  std::optional<T> value_;
  std::optional<Error> error_;
};

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
  return ValueType(std::move(t));
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
    tmp.value();                               \
  })

}  // namespace afc::language
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
