#include "src/language/error/value_or_error.h"

#include "glog/logging.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::lazy_string::LazyString;

namespace afc::language {
Error::Error(std::wstring input) : Error{LazyString{std::move(input)}} {}

Error AugmentError(language::lazy_string::LazyString prefix, Error error) {
  return Error{prefix + LazyString{L": "} + error.read()};
}

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator) {
  CHECK(!errors.empty());
  return Error(Concatenate(errors | std::views::transform([](const Error& e) {
                             return LazyString{e.read()};
                           }) |
                           Intersperse(LazyString{separator})));
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
}  // namespace
}  // namespace afc::language
