#include "src/tests/tests.h"

#include <glog/logging.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <unordered_map>
#include <unordered_set>

namespace afc::tests {
namespace {
std::unordered_map<std::wstring, std::vector<Test>>* tests_map() {
  static std::unordered_map<std::wstring, std::vector<Test>> output;
  return &output;
}

int ForkAndWait(std::function<void()> callable) {
  pid_t p = fork();
  if (p == -1) LOG(FATAL) << "Fork failed.";

  if (p == 0) {
    LOG(INFO) << "Child process: starting callback.";
    callable();
    LOG(INFO) << "Child process didn't crash; will exit successfully.";
    exit(0);
  }

  int wstatus;
  LOG(INFO) << "Parent process: waiting for child: " << p;
  if (waitpid(p, &wstatus, 0) == -1) LOG(FATAL) << "Waitpid failed.";
  return wstatus;
}
}  // namespace

bool Register(std::wstring name, std::vector<Test> tests) {
  CHECK_GT(tests.size(), 0ul);
  std::unordered_set<std::wstring> test_names;
  for (const auto& test : tests) {
    CHECK(test_names.insert(test.name).second)
        << "Repeated test name: " << name << ": " << test.name;
  }
  for (const auto& test : tests) {
    CHECK_LT(test.runs, 1000000ul);
  }
  auto [_, result] = tests_map()->insert({name, std::move(tests)});
  CHECK(result) << "Unable to insert tests (repeated name for group?): "
                << name;
  return true;
}

void Run(std::vector<std::wstring> tests_filter) {
  std::unordered_map<std::wstring, std::vector<std::wstring>> failures;
  std::cerr << "# Test Groups" << std::endl << std::endl;
  size_t executions = 0;
  const std::unordered_set<std::wstring> tests_filter_set(tests_filter.begin(),
                                                          tests_filter.end());
  CHECK_EQ(tests_filter_set.size(), tests_filter.size());
  for (const auto& [name, tests] : *tests_map()) {
    bool printed_group_name = false;
    for (const auto& test : tests) {
      if (tests_filter_set.empty() ||
          tests_filter_set.find(name + L"." + test.name) !=
              tests_filter_set.end()) {
        executions++;
        if (!printed_group_name)
          std::cerr << "## Group: " << name << std::endl << std::endl;
        printed_group_name = true;
        std::cerr << "* " << test.name << std::endl;
        for (size_t i = 0;
             i < std::max(test.runs, tests_filter_set.empty() ? 0ul : 1ul);
             ++i) {
          if (tests_filter_set.size() == 1) {
            // We prefer not to fork if we're just running a single test. That
            // makes it easier to do things like attach a debugger.
            test.callback();
          } else {
            int wstatus = ForkAndWait(test.callback);
            if (!WIFEXITED(wstatus)) {
              failures[name].push_back(test.name);
              std::cerr << "Didn't exit" << std::endl;
            } else if (WEXITSTATUS(wstatus) != 0) {
              failures[name].push_back(test.name);
              std::cerr << "Exit status: " << WEXITSTATUS(wstatus) << std::endl;
            }
          }
        }
      }
    }
    if (printed_group_name) std::cerr << std::endl;
  }
  if (!tests_filter_set.empty()) CHECK_EQ(executions, tests_filter_set.size());
  if (!failures.empty()) {
    std::cerr << "# Failures" << std::endl << std::endl;
    for (auto& [group, tests] : failures) {
      std::cerr << "* " << group << std::endl;
      for (auto& test : tests) std::cerr << "  * " << test << std::endl;
    }
    std::cerr << std::endl;
  }
  CHECK_EQ(failures.size(), 0ul);
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

void ForkAndWaitForFailure(std::function<void()> callable) {
  int wstatus = ForkAndWait(std::move(callable));
  CHECK(!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0);
}

}  // namespace afc::tests
