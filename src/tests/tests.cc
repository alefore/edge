#include "src/tests/tests.h"

namespace afc::tests {
namespace {
std::unordered_map<std::wstring, std::vector<Test>>* tests_map() {
  static std::unordered_map<std::wstring, std::vector<Test>> output;
  return &output;
}
}  // namespace

bool Register(std::wstring name, std::vector<Test> tests) {
  CHECK_GT(tests.size(), 0ul);
  auto [_, result] = tests_map()->insert({name, std::move(tests)});
  CHECK(result) << "Unable to insert tests (repeated name for group?): "
                << name;
  return true;
}

void Run() {
  std::cerr << "# Test Groups" << std::endl << std::endl;
  for (const auto& [name, tests] : *tests_map()) {
    std::cerr << "## Group: " << name << std::endl << std::endl;
    for (const auto& test : tests) {
      std::cerr << "* " << test.name << std::endl;
      test.callback();
    }
  }
}

void List() {
  std::cerr << "Available tests:" << std::endl;
  for (const auto& [name, tests] : *tests_map()) {
    std::cerr << "* " << name << std::endl;
    for (const auto& test : tests) {
      std::cerr << "  * " << test.name << std::endl;
    }
  }
}
}  // namespace afc::tests
