#include "src/infrastructure/command_line.h"

#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::lazy_string::LazyString;

namespace afc::command_line_arguments {
void HonorStandardArguments(const StandardArguments& arguments) {
  if (!arguments.tests_filter.empty()) {
    tests::Run(container::MaterializeVector(
        arguments.tests_filter | std::views::transform(&LazyString::ToString)));
    exit(0);
  }
  switch (arguments.tests_behavior) {
    case TestsBehavior::kRunAndExit:
      tests::Run(container::MaterializeVector(
          arguments.tests_filter |
          std::views::transform(&LazyString::ToString)));
      exit(0);
    case TestsBehavior::kListAndExit:
      tests::List();
      exit(0);
    case TestsBehavior::kIgnore:
      break;
  }
}
}  // namespace afc::command_line_arguments
