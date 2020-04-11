#ifndef __AFC_EDITOR_TESTS_TESTS_H__
#define __AFC_EDITOR_TESTS_TESTS_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>

#include "src/fuzz_testable.h"
#include "src/line_column.h"
#include "src/wstring.h"

namespace afc::tests {
struct Test {
  std::wstring name;
  std::function<void()> callback;
};

bool Register(std::wstring name, std::vector<Test> tests);
void Run();
void List();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_TESTS_H__
