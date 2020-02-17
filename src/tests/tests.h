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

template <typename T>
class TestGroup {
 public:
  virtual std::wstring Name() const = 0;
  virtual std::vector<Test> Tests() const = 0;

 private:
  static const bool registration_;
};

bool Register(std::wstring name, std::vector<Test> tests);

template <typename T>
bool Add() {
  return Register(T().Name(), T().Tests());
}

void Run();
void List();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_TESTS_H__
