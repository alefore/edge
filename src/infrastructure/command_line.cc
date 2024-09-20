#include "src/infrastructure/command_line.h"

#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::command_line_arguments {

FlagName::FlagName(std::wstring input)
    : FlagName(NonEmptySingleLine{SingleLine{LazyString{input}}}) {}

const LazyString& FlagName::GetLazyString() const {
  return GetSingleLine().read();
}

const SingleLine& FlagName::GetSingleLine() const { return read().read(); }

FlagShortHelp::FlagShortHelp(std::wstring input)
    : FlagShortHelp(NonEmptySingleLine{SingleLine{LazyString{input}}}) {}

const LazyString& FlagShortHelp::GetLazyString() const {
  return read().read().read();
}

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
