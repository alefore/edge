// Example:
//
// TEST_GROUP(LazyString_StartsWith,
//            [](LazyString x, LazyString y) {
//              return StartsWith(x, y);
//            })
//     .Add(L"AllEmpty", L"", L"", true)
//     .Add(L"EmptyInput", L"", L"foo", false)
//     .Add(L"EmptyPrefix", L"foo", L"", true)

#pragma once

#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/tests/tests.h"

namespace afc::tests {
namespace internal {
template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct is_variant : std::false_type {};

template <typename... Args>
struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T>
inline constexpr bool is_variant_v = is_variant<T>::value;

template <typename T>
std::string Stringify(const T& value) {
  if constexpr (requires(std::ostream& os) { os << value; }) {
    std::stringstream ss;
    ss << value;
    return ss.str();
  } else if constexpr (is_optional_v<T>) {
    if (!value) return "nullopt";
    return "optional(" + Stringify(*value) + ")";
  } else if constexpr (is_variant_v<T>) {
    return std::visit([](const auto& arg) { return Stringify(arg); }, value);
  } else {
    return "{unprintable type}";
  }
}
}  // namespace internal

template <typename Output, typename... Inputs>
class TestGroupFactory {
 public:
  using Callback = std::function<Output(Inputs...)>;

 private:
  const std::wstring group_name_;
  const Callback callback_;
  std::vector<Test> tests_;
  bool registered_ = false;

 public:
  TestGroupFactory(std::wstring group_name, Callback callback)
      : group_name_(std::move(group_name)), callback_(std::move(callback)) {
    CHECK(!group_name_.empty());
    CHECK(callback_);
  }

  TestGroupFactory& Add(std::wstring name, Inputs... inputs, Output output) {
    tests_.push_back(Test{
        .name = name,
        .callback = [inputs_tuple = std::make_tuple(std::move(inputs)...),
                     output, callback = callback_] {
          Output result = std::apply(callback, inputs_tuple);
          CHECK(result == output) << "Expected " << internal::Stringify(output)
                                  << " but got " << internal::Stringify(result);
        }});
    return *this;
  };

  operator bool() {
    CHECK(!registered_);
    registered_ = true;
    return tests::Register(group_name_, std::move(tests_));
  }
};

#define TESTS_CONCAT_IMPL(a, b) a##b
#define TESTS_CONCAT(a, b) TESTS_CONCAT_IMPL(a, b)
#define TESTS_WIDEN_STRING(x) TESTS_CONCAT(L, #x)

#define TEST_GROUP(name, callback)                                         \
  const bool TESTS_CONCAT(global_register_test_, __LINE__) =               \
      afc::tests::internal::MakeTestGroupFactory(TESTS_WIDEN_STRING(name), \
                                                 callback)

// Internal helpers to deduce types.
namespace internal {
template <typename Output, typename... Inputs, typename Callable>
auto MakeTestGroupFactoryImpl(std::wstring name, Callable&& callback) {
  return TestGroupFactory<Output, Inputs...>(std::move(name),
                                             std::forward<Callable>(callback));
}

template <typename Callable>
auto MakeTestGroupFactory(std::wstring name, Callable&& callback) {
  std::function target(std::forward<Callable>(callback));
  return [&name]<typename Output, typename... Inputs>(
             Callable&& cb, std::function<Output(Inputs...)>*) {
    return TestGroupFactory<Output, Inputs...>(std::move(name),
                                               std::forward<Callable>(cb));
  }(std::forward<Callable>(callback), &target);
}
}  // namespace internal
}  // namespace afc::tests
