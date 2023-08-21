#include "src/tests/fuzz_testable.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "glog/logging.h"
#include "src/language/wstring.h"

namespace afc {
namespace editor {
namespace fuzz {

/* static */
void FuzzTestable::Test(Stream& input, FuzzTestable* fuzz_testable) {
  auto handlers = fuzz_testable->FuzzHandlers();
  CHECK_LT(handlers.size(), 256ul);
  while (true) {
    unsigned char choice;
    if (!(input >> choice)) {
      VLOG(4) << "Done fuzzing.";
      return;
    }
    choice %= handlers.size();
    VLOG(5) << "Next handler choice: " << static_cast<int>(choice);
    handlers[choice](input);
  }
}

}  // namespace fuzz
}  // namespace editor
}  // namespace afc
