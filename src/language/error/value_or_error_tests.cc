#include "src/language/error/value_or_error.h"
#include "src/tests/tests.h"

namespace afc::language {
namespace {
bool tests_register = tests::Register(
    L"ValueOrError", {{.name = L"EmptyConstructor", .callback = [] {
                         ValueOrError<int> foo;
                         CHECK(!IsError(foo));
                         CHECK_EQ(std::get<int>(foo), int());
                       }}});
}  // namespace
}  // namespace afc::language