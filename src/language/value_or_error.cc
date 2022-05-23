#include "src/language/value_or_error.h"

#include "glog/logging.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language {

std::ostream& operator<<(std::ostream& os, const Error& p) {
  os << "[Error: " << p.description << "]";
  return os;
}

ValueOrError<EmptyValue> Success() { return ValueType(EmptyValue()); }

namespace {
bool tests_register = tests::Register(
    L"ValueOrError", {{.name = L"EmptyConstructor", .callback = [] {
                         ValueOrError<int> foo;
                         CHECK(!foo.IsError());
                         CHECK_EQ(foo.value(), int());
                       }}});
}
}  // namespace afc::language
