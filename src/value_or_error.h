#ifndef __AFC_EDITOR_VALUE_OR_ERROR_H__
#define __AFC_EDITOR_VALUE_OR_ERROR_H__

#include <optional>

namespace afc::editor {
template <typename T>
struct ValueOrError {
  static ValueOrError<T> Value(T t) { return ValueOrError<T>(std::move(t)); }

  static ValueOrError<T> Error(std::wstring error) {
    ValueOrError<T> output;
    output.error = error;
    return output;
  }

  bool IsError() const { return error.has_value(); }

  // Exactly one of these should have a value.
  std::optional<T> value = std::nullopt;
  std::optional<std::wstring> error = std::nullopt;

 private:
  ValueOrError() = default;
  ValueOrError(T value) : value(value) {}
};

struct EmptyValue {};
using PossibleError = ValueOrError<EmptyValue>;

PossibleError Success();
PossibleError Error(std::wstring description);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_VALUE_OR_ERROR_H__
