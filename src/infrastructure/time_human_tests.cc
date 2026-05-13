// Tests for //src/infrastructure:time_human.
//
// These tests are in a separate module to allow //src/tests to depend on
// //src/infrastructure:time_human; otherwise we'd have circular dependencies.

#include <glog/logging.h>

#include "src/infrastructure/time_human.h"
#include "src/tests/tests.h"

namespace afc::infrastructure {
namespace {
const bool duration_to_string_tests_registration = tests::Register(
    L"DurationToSTring", {{.name = L"Simple", .callback = [] {
                             CHECK_EQ(DurationToString(12.2),
                                      NON_EMPTY_SINGLE_LINE_CONSTANT(L"12s"));
                           }}});
}  // namespace
}  // namespace afc::infrastructure
