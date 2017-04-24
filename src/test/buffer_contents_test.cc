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

void TestBufferInsertModifiers() {
  BufferContents contents;
  Line::Options options;
  options.contents = NewCopyString(L"alejo");
  options.modifiers.assign(5, {Line::CYAN});

  contents.push_back(std::make_shared<Line>(options));
  contents.push_back(std::make_shared<Line>(options));
  options.modifiers[2].insert(Line::BOLD);
  contents.push_back(std::make_shared<Line>(options));
  auto line = std::make_shared<Line>(*contents.at(1));
  line->SetAllModifiers(Line::ModifiersSet({Line::DIM}));
  contents.push_back(line);

  for (int i = 0; i < 2; i++) {
    LOG(INFO) << "Start iteration: " << i;
    CHECK_EQ(contents.size(), 4);

    CHECK(contents.at(0)->modifiers()[0] == Line::ModifiersSet({Line::CYAN}));
    CHECK(contents.at(0)->modifiers()[1] == Line::ModifiersSet({Line::CYAN}));
    CHECK(contents.at(0)->modifiers()[2] == Line::ModifiersSet({Line::CYAN}));

    CHECK(contents.at(1)->modifiers()[0] == Line::ModifiersSet({Line::CYAN}));
    CHECK(contents.at(1)->modifiers()[2] == Line::ModifiersSet({Line::CYAN}));

    CHECK(contents.at(2)->modifiers()[0] == Line::ModifiersSet({Line::CYAN}));
    CHECK(contents.at(2)->modifiers()[2] ==
              Line::ModifiersSet({Line::CYAN, Line::BOLD}));

    CHECK(contents.at(3)->modifiers()[0] == Line::ModifiersSet({Line::DIM}));
    CHECK(contents.at(3)->modifiers()[2] == Line::ModifiersSet({Line::DIM}));

    contents.SplitLine(LineColumn(0, 2));
    CHECK_EQ(contents.size(), 5);
    contents.FoldNextLine(0);
    CHECK_EQ(contents.size(), 4);

    contents.SplitLine(LineColumn(3, 2));
    CHECK_EQ(contents.size(), 5);
    contents.FoldNextLine(3);
    CHECK_EQ(contents.size(), 4);
  }
}
}  // namespace

void BufferContentsTests() {
  LOG(INFO) << "BufferContents tests: start.";
  TestBufferContentsSnapshot();
  TestBufferInsertModifiers();
  LOG(INFO) << "BufferContents tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
