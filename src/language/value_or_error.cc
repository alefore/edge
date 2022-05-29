#include "src/language/value_or_error.h"

#include "glog/logging.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language {
Error AugmentError(std::wstring prefix, Error error) {
  return Error(prefix + L": " + error.read());
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
