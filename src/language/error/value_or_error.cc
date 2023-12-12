#include "src/language/error/value_or_error.h"

#include "glog/logging.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::lazy_string::NewLazyString;

namespace afc::language {
Error NewError(lazy_string::LazyString error) {
  return Error(error.ToString());
}

Error AugmentError(std::wstring prefix, Error error) {
  return Error(prefix + L": " + error.read());
}

// Precondition: `errors` must be non-empty.
Error MergeErrors(const std::vector<Error>& errors,
                  const std::wstring& separator) {
  CHECK(!errors.empty());
  return Error(Concatenate(errors | std::views::transform([](const Error& e) {
                             return NewLazyString(e.read());
                           }) |
                           Intersperse(NewLazyString(separator)))
                   .ToString());
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
