#include "src/tests/tests.h"

#include <glog/logging.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/language/container.h"

namespace container = afc::language::container;

using afc::language::InsertOrDie;

namespace afc::tests {
namespace {
std::unordered_map<std::wstring, std::vector<Test>>* tests_map() {
  static std::unordered_map<std::wstring, std::vector<Test>> output;
  return &output;
}

struct TestInfoToSchedule {
  std::wstring group_name;
  std::wstring test_name;
  const Test& test;
};

struct TestCompletionReport {
  std::wstring group_name;
  std::wstring test_name;
  std::optional<int> wait_status = std::nullopt;
};

std::vector<TestInfoToSchedule> GetSchedule(
    const std::unordered_set<std::wstring>& tests_filter_set) {
  std::vector<TestInfoToSchedule> output;
  // First pass: Identify all tests to run.
  for (const std::pair<const std::wstring, std::vector<Test>>& group_pair :
       *tests_map()) {
    const std::wstring& name = group_pair.first;
    const std::vector<Test>& tests = group_pair.second;
    for (const Test& test_obj : tests)
      if (tests_filter_set.empty() ||
          tests_filter_set.contains(name + L"." + test_obj.name) ||
          tests_filter_set.contains(name))
        output.push_back({name, test_obj.name, test_obj});
  }
  return output;
}

pid_t ForkTest(const TestInfoToSchedule& info) {
  pid_t child_pid = fork();
  if (child_pid == -1) LOG(FATAL) << "Fork failed.";

  if (child_pid != 0) return child_pid;
  // Child process: execute the test callback
  LOG(INFO) << "Child process: starting callback for " << info.group_name
            << L"." << info.test_name;
  for (size_t i = 0; i < info.test.runs; ++i) info.test.callback();
  exit(0);  // Child process exits successfully after completing its task
}
}  // namespace

bool Register(std::wstring name, std::vector<Test> tests) {
  CHECK_GT(tests.size(), 0ul);
  std::unordered_set<std::wstring> test_names;
  for (const Test& test_obj : tests) InsertOrDie(test_names, test_obj.name);
  for (const Test& test_obj : tests) CHECK_LT(test_obj.runs, 1000000ul);
  InsertOrDie(*tests_map(), {name, std::move(tests)});
  return true;
}

void Run(std::vector<std::wstring> tests_filter) {
  std::cerr << "# Test Groups" << std::endl << std::endl;
  const std::unordered_set<std::wstring> tests_filter_set(tests_filter.begin(),
                                                          tests_filter.end());
  CHECK_EQ(tests_filter_set.size(), tests_filter.size());

  const std::vector<TestInfoToSchedule> tests_to_schedule =
      GetSchedule(tests_filter_set);

  // If we have a single test, avoid forking.
  if (tests_to_schedule.size() == 1) {
    const TestInfoToSchedule& test = tests_to_schedule.front();
    std::cerr << "## Group: " << test.group_name << std::endl << std::endl;
    std::cerr << "* " << test.test_name << std::endl;
    for (size_t i = 0; i < test.test.runs; ++i) test.test.callback();
    std::cerr << std::endl;
    return;
  }

  std::map<std::wstring, std::map<std::wstring, int>> execution_results;
  std::map<std::wstring, std::set<std::wstring>> failures;
  std::unordered_map<pid_t, TestInfoToSchedule> running_tests;

  const size_t kMaxConcurrentTests = 32;
  size_t next_test_to_launch_index = 0;

  while (next_test_to_launch_index < tests_to_schedule.size() ||
         !running_tests.empty()) {
    // Launch new tests as long as we are under capacity and have tests left to
    // schedule
    while (running_tests.size() < kMaxConcurrentTests &&
           next_test_to_launch_index < tests_to_schedule.size()) {
      const TestInfoToSchedule& test_to_launch =
          tests_to_schedule[next_test_to_launch_index];
      const pid_t child_pid = ForkTest(test_to_launch);
      running_tests.emplace(child_pid, test_to_launch);
      next_test_to_launch_index++;
    }

    if (!running_tests.empty()) {
      LOG(INFO) << "Waiting for a test to complete.";

      int wait_status;
      // Wait for any child process to change state (block until one finishes)
      pid_t completed_pid = waitpid(-1, &wait_status, 0);
      if (completed_pid == -1) LOG(FATAL) << "Waitpid failed.";

      CHECK(running_tests.contains(completed_pid))
          << "Completed PID " << completed_pid
          << " not found in running_tests map.";
      const TestInfoToSchedule& info =
          running_tests.find(completed_pid)->second;

      CHECK(!execution_results[info.group_name].contains(info.test_name));
      execution_results[info.group_name][info.test_name] = wait_status;
      running_tests.erase(completed_pid);  // Clean up from tracking maps
    }
  }

  LOG(INFO) << "All tests have completed; producing final output.";
  for (const std::pair<const std::wstring, std::map<std::wstring, int>>& group :
       execution_results) {
    std::cerr << "## Group: " << group.first << std::endl << std::endl;
    for (const std::pair<const std::wstring, int>& result : group.second) {
      std::cerr << "* " << result.first;
      if (!WIFEXITED(result.second)) {
        failures[group.first].insert(result.first);
        std::cerr << ": Didn't exit" << std::endl;
      } else if (WEXITSTATUS(result.second) != 0) {
        failures[group.first].insert(result.first);
        std::cerr << ": Exit status: " << WEXITSTATUS(result.second)
                  << std::endl;
      }
      std::cerr << std::endl;
    }
    std::cerr << std::endl;
  }

  const size_t executions = container::Sum(
      execution_results | std::views::values |
      std::views::transform([](const std::map<std::wstring, int>& group_data) {
        return group_data.size();
      }));

  // Final summary
  if (!failures.empty()) {
    std::cerr << "# Failures" << std::endl;
    for (const std::pair<const std::wstring, std::set<std::wstring>>&
             group_data : failures) {
      std::cerr << "* " << group_data.first << std::endl;
      for (const std::wstring& test_name : group_data.second)
        std::cerr << "  * " << test_name << std::endl;
    }
    std::cerr << std::endl;
  }

  std::cerr << "# Test results" << std::endl << std::endl;
  std::cerr << "Tests executed: " << executions << std::endl;
  std::cerr << "Tests failures: "
            << container::Sum(failures | std::views::values |
                              std::views::transform(
                                  [](const std::set<std::wstring>& data) {
                                    return data.size();
                                  }))
            << std::endl;
  CHECK_EQ(failures.size(), 0ul);
}  // namespace

void List() {
  std::cerr << "Available tests:" << std::endl;
  for (const std::pair<const std::wstring, std::vector<Test>>& group_pair :
       *tests_map()) {
    const std::wstring& name = group_pair.first;
    const std::vector<Test>& tests_in_group = group_pair.second;
    std::cerr << "* " << name << std::endl;
    for (const Test& test_obj : tests_in_group) {
      std::cerr << "  * " << test_obj.name << std::endl;
    }
  }
}

// Helper for tests that specifically expect a child process to fail/crash.
void ForkAndWaitForFailure(std::function<void()> callable) {
  pid_t child_pid = fork();
  if (child_pid == -1) LOG(FATAL) << "Fork failed.";

  if (child_pid == 0) {
    LOG(INFO) << "Child process: starting callback for ForkAndWaitForFailure.";
    callable();
    LOG(INFO) << "Child process didn't crash; will exit successfully.";
    exit(0);
  }

  int wait_status;
  LOG(INFO) << "Parent process: waiting for child (ForkAndWaitForFailure): "
            << child_pid;
  if (waitpid(child_pid, &wait_status, 0) == -1)
    LOG(FATAL) << "Waitpid failed.";
  // Original check: assert that the child either didn't exit normally or
  // exited with non-zero status.
  CHECK(!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0);
}

}  // namespace afc::tests
