#ifndef __AFC_EDITOR_TESTS_FUZZ_TESTABLE_H__
#define __AFC_EDITOR_TESTS_FUZZ_TESTABLE_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>

namespace afc::editor::fuzz {

using Stream = std::ifstream;
using Handler = std::function<void(Stream&)>;

class FuzzTestable {
 public:
  static void Test(Stream& input, FuzzTestable* testable);

  virtual std::vector<Handler> FuzzHandlers() = 0;
};

}  // namespace afc::editor::fuzz

#endif  // __AFC_EDITOR_TESTS_FUZZ_TESTABLE_H__
