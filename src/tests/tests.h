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
#include <vector>

#include "src/language/wstring.h"

namespace afc::tests {
struct Test {
  std::wstring name;
  size_t runs = 1;
  std::function<void()> callback;
};

bool Register(std::wstring name, std::vector<Test> tests);
void Run();
void List();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_TESTS_H__
