#ifndef __AFC_EDITOR_TESTS_TESTS_H__
#define __AFC_EDITOR_TESTS_TESTS_H__

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "src/language/wstring.h"

namespace afc::tests {
struct Test {
  std::wstring name;
  size_t runs = 1;
  std::function<void()> callback;
};

bool Register(std::wstring name, std::vector<Test> tests);
void Run();
void List();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_TESTS_H__
