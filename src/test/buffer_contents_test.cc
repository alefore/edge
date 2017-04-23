#include "src/test/line_test.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_contents.h"
#include "src/char_buffer.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
void TestBufferContentsSnapshot() {
  BufferContents contents;
  for (auto& s : { L"alejandro", L"forero", L"cuervo" }) {
    contents.push_back(std::make_shared<Line>(Line::Options(NewCopyString(s))));
  }
  auto copy = contents.copy();
  CHECK_EQ("alejandro\nforero\ncuervo",
           ToByteString(contents.ToString()));
  CHECK_EQ("alejandro\nforero\ncuervo",
           ToByteString(copy->ToString()));

  contents.SplitLine(LineColumn(1, 3));
  CHECK_EQ("alejandro\nfor\nero\ncuervo",
           ToByteString(contents.ToString()));
  CHECK_EQ("alejandro\nforero\ncuervo",
           ToByteString(copy->ToString()));
}
}  // namespace

void BufferContentsTests() {
  LOG(INFO) << "BufferContents tests: start.";
  TestBufferContentsSnapshot();
  LOG(INFO) << "BufferContents tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
