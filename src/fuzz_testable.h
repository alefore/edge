#ifndef __AFC_EDITOR_FUZZ_TESTABLE_H__
#define __AFC_EDITOR_FUZZ_TESTABLE_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>

namespace afc {
namespace editor {
namespace fuzz {

using Stream = std::ifstream;
using Handler = std::function<void(Stream&)>;

class FuzzTestable {
 public:
  static void Test(Stream& input, FuzzTestable* testable);

  virtual std::vector<Handler> FuzzHandlers() = 0;
};

}  // namespace fuzz
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FUZZ_TESTABLE_H__
