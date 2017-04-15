#include "src/test/line_test.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/char_buffer.h"
#include "src/line.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
template <typename C, typename V>
void CheckSingleton(C const container, V value) {
  CHECK_EQ(container.size(), 1);
  CHECK(container.find(value) != container.end());
}

void TestLineDeleteCharacters() {
  // Preparation.
  Line line(Line::Options(NewCopyCharBuffer(L"alejo")));
  line.modifiers()[0].insert(Line::RED);
  line.modifiers()[1].insert(Line::GREEN);
  line.modifiers()[2].insert(Line::BLUE);
  line.modifiers()[3].insert(Line::BOLD);
  line.modifiers()[4].insert(Line::DIM);

  {
    Line line_copy(line);
    line_copy.DeleteCharacters(2);
    CHECK_EQ(ToByteString(line_copy.contents()->ToString()), "al");
    CHECK_EQ(line_copy.modifiers().size(), 2);
    CheckSingleton(line_copy.modifiers()[0], Line::RED);
    CheckSingleton(line_copy.modifiers()[1], Line::GREEN);
  }

  {
    Line line_copy(line);
    line_copy.DeleteCharacters(1, 2);
    CHECK_EQ(ToByteString(line_copy.contents()->ToString()), "ajo");
    CHECK_EQ(line_copy.modifiers().size(), 3);
    CheckSingleton(line_copy.modifiers()[0], Line::RED);
    CheckSingleton(line_copy.modifiers()[1], Line::BOLD);
    CheckSingleton(line_copy.modifiers()[2], Line::DIM);
  }

  // Original isn't modified.
  CHECK_EQ(line.size(), 5);
  CHECK_EQ(line.modifiers().size(), 5);
  CheckSingleton(line.modifiers()[0], Line::RED);
  CheckSingleton(line.modifiers()[1], Line::GREEN);
  CheckSingleton(line.modifiers()[2], Line::BLUE);
  CheckSingleton(line.modifiers()[3], Line::BOLD);
  CheckSingleton(line.modifiers()[4], Line::DIM);
}
}  // namespace

void LineTests() {
  LOG(INFO) << "Line tests: start.";
  TestLineDeleteCharacters();
  LOG(INFO) << "Line tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
