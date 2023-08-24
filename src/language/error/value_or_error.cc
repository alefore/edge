#include "src/language/error/value_or_error.h"

#include "glog/logging.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language {
Error AugmentError(std::wstring prefix, Error error) {
  return Error(prefix + L": " + error.read());
}

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator) {
  CHECK(!errors.empty());
  std::wstring error_description;
  std::wstring current_separator = L"";
  for (const Error& error : errors) {
    error_description += current_separator + error.read();
    current_separator = separator;
  }
  return Error(std::move(error_description));
}

ValueOrError<EmptyValue> Success() {
  return ValueOrError<EmptyValue>(EmptyValue());
}

void IgnoreErrors::operator()(Error) {}

namespace {
bool tests_register = tests::Register(
    L"ValueOrError", {{.name = L"EmptyConstructor", .callback = [] {
                         ValueOrError<int> foo;
                         CHECK(!IsError(foo));
                         CHECK_EQ(std::get<int>(foo), int());
                       }}});
}
}  // namespace afc::language
