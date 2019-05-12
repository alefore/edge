#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_contents.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/viewers.h"
#include "src/wstring.h"

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
