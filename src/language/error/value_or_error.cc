#include "src/language/error/value_or_error.h"

#include "glog/logging.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"

using afc::language::lazy_string::LazyString;

namespace afc::language {
Error AugmentError(language::lazy_string::LazyString prefix, Error error) {
  return Error{prefix + LazyString{L": "} + error.read()};
}

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator) {
  CHECK(!errors.empty());
  return Error(Concatenate(
      errors | std::views::transform([](const Error& e) { return e.read(); }) |
      Intersperse(LazyString{separator})));
}

ValueOrError<EmptyValue> Success() {
  return ValueOrError<EmptyValue>(EmptyValue());
}

void IgnoreErrors::operator()(Error) {}
}  // namespace afc::language
