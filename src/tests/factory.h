// Example:
//
// TEST_GROUP(LazyString_StartsWith,
//            [](std::pair<LazyString, LazyString> input) {
//              return StartsWith(input.first, input.second);
//            })
//     .Add(L"AllEmpty", {L"", L""}, true)
//     .Add(L"EmptyInput", {L"", L"foo"}, false)
//     .Add(L"EmptyPrefix", {L"foo", L""}, true)

#ifndef __AFC_EDITOR_SRC_TESTS_FACTORY_H__
#define __AFC_EDITOR_SRC_TESTS_FACTORY_H__

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
std::string Stringify(const T& value) {
  if constexpr (requires(std::ostream& os) { os << value; }) {
    std::stringstream ss;
    ss << value;
    return ss.str();
  } else if constexpr (is_optional_v<T>) {
    if (!value) return "nullopt";
    return "optional(" + Stringify(*value) + ")";
  } else {
    return "{unprintable type}";
  }
}
}  // namespace internal

template <typename Input, typename Output>
class TestGroupFactory {
 public:
  using Callback = std::function<Output(Input)>;

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

  TestGroupFactory& Add(std::wstring name, Input input, Output output) {
    tests_.push_back(Test{
        .name = name, .callback = [input, output, callback = callback_] {
          Output result = callback(input);
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
template <typename T>
struct callable_traits;

template <typename R, typename Arg>
struct callable_traits<R(Arg)> {
  using Input = std::decay_t<Arg>;
  using Output = std::decay_t<R>;
};

template <typename T>
struct callable_traits : callable_traits<decltype(&T::operator())> {};

template <typename C, typename R, typename Arg>
struct callable_traits<R (C::*)(Arg) const> : callable_traits<R(Arg)> {};

template <typename Callable>
auto MakeTestGroupFactory(std::wstring name, Callable&& callback) {
  using Traits = callable_traits<std::decay_t<Callable>>;
  return TestGroupFactory<typename Traits::Input, typename Traits::Output>(
      std::move(name), std::forward<Callable>(callback));
}
}  // namespace internal
}  // namespace afc::tests

#endif  // __AFC_EDITOR_SRC_TESTS_FACTORY_H__
