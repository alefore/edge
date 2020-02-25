#include "src/value_or_error.h"

namespace afc::editor {

PossibleError Success() {
  return ValueOrError<EmptyValue>::Value(EmptyValue());
}

PossibleError Error(std::wstring description) {
  return ValueOrError<EmptyValue>::Error(description);
}

}  // namespace afc::editor
