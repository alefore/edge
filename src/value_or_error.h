#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <optional>

namespace afc::editor {
struct Error {
  Error(std::wstring description) : description(std::move(description)) {}
  std::wstring description;
};

template <typename T>
struct ValueType {
  ValueType(T value) : value(std::move(value)) {}
  T value;
};

template <typename T>
struct ValueOrError {
  using ValueType = T;

  ValueOrError(Error error) : error(std::move(error.description)) {}
  ValueOrError(editor::ValueType<T> value) : value(std::move(value.value)) {}

  bool IsError() const { return error.has_value(); }

  // Exactly one of these should have a value.
  std::optional<T> value = std::nullopt;
  std::optional<std::wstring> error = std::nullopt;
};

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

}  // namespace afc::editor
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
