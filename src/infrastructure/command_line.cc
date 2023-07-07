#include "src/infrastructure/command_line.h"

#include "src/tests/tests.h"

namespace afc::command_line_arguments {
void HonorStandardArguments(const StandardArguments& arguments) {
  switch (arguments.tests_behavior) {
    case TestsBehavior::kRunAndExit:
      tests::Run();
      exit(0);
    case TestsBehavior::kListAndExit:
      tests::List();
      exit(0);
    case TestsBehavior::kIgnore:
      break;
  }
}
}  // namespace afc::command_line_arguments
