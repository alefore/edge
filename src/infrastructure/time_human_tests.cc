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
    L"DurationToSTring", std::invoke([] -> std::vector<tests::Test> {
      auto test = [](double input, std::wstring expected_output) {
        return tests::Test{.name = expected_output,
                           .callback = [input, expected_output] {}};
      };
      return {test(12.2, L"12s"),
              test(0.00001, L"0ms"),
              test(0.0048, L"4ms"),
              test(59.8, L"59s"),
              test(119.8, L"1m"),
              test(3599.8, L"59m"),
              test(3600.1, L"1h"),
              test(24 * 60 * 60 - 0.1, L"23h"),
              test(24 * 60 * 60 + 0.1, L"1d"),
              test(24 * 60 * 60 * 52 + 0.1, L"52d")};
    }));
}  // namespace
}  // namespace afc::infrastructure
