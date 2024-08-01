// Module to define unit tests.
//
// The unit tests are always built into the binary. To run them, the binary
// should call the `Run` method.
//
// To register unit tests, a module does something like this:
//
// const bool bayes_sort_tests_registration = tests::Register(
//     L"BayesSort",
//     {{.name = L"EmptyHistoryAndFeatures",
//       .callback =
//           [] { CHECK_EQ(Sort(History({}), FeaturesSet({})).size(), 0ul); }},
//      {.name = L"SortOk", .callback = ... },
//      ...});
//
// It'll often be helpful to define helper functions, as in:
//
// const bool bayes_sort_tests_registration = tests::Register(
//     L"BayesSort",
//     [] {
//       auto test = [](...) { ... return tests::Test(...); };
//       return std::vector({
//           test(...), test(...), ...});
//     }());
#ifndef __AFC_EDITOR_TESTS_TESTS_H__
#define __AFC_EDITOR_TESTS_TESTS_H__

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/language/wstring.h"

namespace afc::tests {
struct Test {
  std::wstring name;
  // How many times should this test be run by default?
  //
  // If set to 0, the test will only be run if it's selected explicitly (through
  // tests_filter).
  size_t runs = 1;
  std::function<void()> callback;
};

bool Register(std::wstring name, std::vector<Test> tests);
// If non-empty, must match the name of a test ("<group>.<test>").
void Run(std::vector<std::wstring> tests_filter);
void List();

// Call this from a test to evaluate an expression (captured in `callable`) that
// *should* trigger a crash. If the expression finishes successfully (without
// crashing), the test will fail.
void ForkAndWaitForFailure(std::function<void()> callable);

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_TESTS_H__
