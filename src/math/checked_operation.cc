#include "src/math/checked_operation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

#include "src/tests/tests.h"

namespace afc::math::numbers {

const bool multiplication_tests_registration =
    tests::Register(L"math::numbers::CheckedMultiply", [] {
      auto test_int32 = [](std::wstring name, int32_t input1, int32_t input2,
                           int32_t expectation, bool expect_error = false) {
        return tests::Test{
            .name = name,
            .callback = [input1, input2, expectation, expect_error] mutable {
              auto result = CheckedMultiply<int32_t, int32_t>(input1, input2);
              if (expect_error) {
                CHECK(IsError(result));
              } else {
                int32_t result_value = ValueOrDie(std::move(result));
                CHECK_EQ(result_value, expectation)
                    << input1 << " * " << input2 << " == " << result_value;
              }
            }};
      };

      auto test_uint32 = [](std::wstring name, uint32_t input1, uint32_t input2,
                            uint32_t expectation, bool expect_error = false) {
        return tests::Test{
            .name = name,
            .callback = [input1, input2, expectation, expect_error] mutable {
              auto result = CheckedMultiply<uint32_t, uint32_t>(input1, input2);
              if (expect_error) {
                CHECK(IsError(result));
              } else {
                CHECK_EQ(ValueOrDie(std::move(result)), expectation);
              }
            }};
      };

      return std::vector<tests::Test>({
          test_int32(L"PositiveIntegers", 123, 456, 56088),
          test_int32(L"PositiveByNegative", 123, -456, -56088),
          test_int32(L"NegativeByNegative", -123, -456, 56088),
          test_int32(L"NegativeByPositive", -123, 456, -56088),
          test_int32(L"MinPositiveByNegative", 1, -1, -1),
          test_int32(L"MaxIntByMinusOne", std::numeric_limits<int32_t>::max(),
                     -1, -std::numeric_limits<int32_t>::max()),
          test_uint32(L"MaxUIntResult",
                      std::numeric_limits<uint32_t>::max() / 2, 2,
                      std::numeric_limits<uint32_t>::max() - 1),
          test_uint32(L"MaxUIntResultPlusOneOverflows",
                      std::numeric_limits<uint32_t>::max() / 2 + 1, 2, 0, true),
          test_uint32(L"ZeroMultiplication", 0, 123456789, 0),
          test_int32(L"ZeroByPositive", 0, 456, 0),
          test_int32(L"PositiveOverflow", std::numeric_limits<int32_t>::max(),
                     2, 0, true),
          test_int32(L"NegativeOverflow", std::numeric_limits<int32_t>::min(),
                     -1, 0, true),
          test_uint32(L"UIntOverflow", std::numeric_limits<uint32_t>::max(), 2,
                      0, true),
          test_int32(L"IntUnderflow", std::numeric_limits<int32_t>::min(), 2, 0,
                     true),
          test_int32(L"SpecialCaseMinByMinusOne",
                     std::numeric_limits<int32_t>::min(), -1, 0, true),
      });
    }());
}  // namespace afc::math::numbers
