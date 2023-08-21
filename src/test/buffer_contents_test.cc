#include "src/buffer_contents.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/test/line_test.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
using language::MakeNonNullShared;
using language::ToByteString;
using language::lazy_string::ColumnNumber;
using language::lazy_string::NewLazyString;

void TestBufferContentsSnapshot() {
  BufferContents contents;
  for (auto& s : {L"alejandro", L"forero", L"cuervo"}) {
    contents.push_back(
        MakeNonNullShared<Line>(Line::Options(NewLazyString(s))));
  }
  auto copy = contents.copy();
  CHECK_EQ("\nalejandro\nforero\ncuervo", ToByteString(contents.ToString()));
  CHECK_EQ("\nalejandro\nforero\ncuervo", ToByteString(copy->ToString()));

  contents.SplitLine(LineColumn(LineNumber(2), ColumnNumber(3)));
  CHECK_EQ("\nalejandro\nfor\nero\ncuervo", ToByteString(contents.ToString()));
  CHECK_EQ("\nalejandro\nforero\ncuervo", ToByteString(copy->ToString()));
}

void TestBufferInsertModifiers() {
  BufferContents contents;
  Line::Options options;
  options.contents = NewLazyString(L"alejo");
  options.modifiers[ColumnNumber(0)] = {LineModifier::kCyan};

  contents.push_back(MakeNonNullShared<Line>(options));  // LineNumber(1).
  contents.push_back(MakeNonNullShared<Line>(options));  // LineNumber(2).
  options.modifiers[ColumnNumber(2)] = {LineModifier::kBold};
  contents.push_back(MakeNonNullShared<Line>(options));  // LineNumber(3).
  auto line = MakeNonNullShared<Line>(contents.at(LineNumber(1)).value());
  line->SetAllModifiers(LineModifierSet({LineModifier::kDim}));
  contents.push_back(line);  // LineNumber(4).

  for (int i = 0; i < 2; i++) {
    LOG(INFO) << "Start iteration: " << i;
    CHECK_EQ(contents.size(), LineNumberDelta(5));

    {
      // Check line 1: 0:CYAN
      auto modifiers_1 = contents.at(LineNumber(1))->modifiers();
      CHECK_EQ(modifiers_1.size(), 1ul);
      CHECK(modifiers_1.find(ColumnNumber(0))->second ==
            LineModifierSet({LineModifier::kCyan}));
    }

    {
      // Check line 2: 0:CYAN
      auto modifiers_2 = contents.at(LineNumber(2))->modifiers();
      CHECK_EQ(modifiers_2.size(), 1ul);
      CHECK(modifiers_2.find(ColumnNumber(0))->second ==
            LineModifierSet({LineModifier::kCyan}));
    }

    {
      // Check line 3: 0:CYAN, 2:BOLD
      auto modifiers_3 = contents.at(LineNumber(3))->modifiers();
      CHECK_EQ(modifiers_3.size(), 2ul);
      CHECK(modifiers_3.find(ColumnNumber(0))->second ==
            LineModifierSet({LineModifier::kCyan}));
      CHECK(modifiers_3.find(ColumnNumber(2))->second ==
            LineModifierSet({LineModifier::kBold}));
    }

    {
      // Check line 4: 0:DIM
      auto modifiers_4 = contents.at(LineNumber(4))->modifiers();
      CHECK_EQ(modifiers_4.size(), 1ul);
      CHECK(modifiers_4.find(ColumnNumber(0))->second ==
            LineModifierSet({LineModifier::kDim}));
    }

    // Contents:
    //
    // alejo 0:C
    // alejo 0:C
    // alejo 0:C 2:B
    // alejo 0:D
    contents.SplitLine(LineColumn(LineNumber(1), ColumnNumber(2)));

    // Contents:
    //
    // al 0:C
    // ejo 0:C
    // alejo 0:C
    // alejo 0:C 2:B
    // alejo 0:D
    CHECK_EQ(contents.size(), LineNumberDelta(6));
    contents.FoldNextLine(LineNumber(1));

    // Contents:
    //
    // alejo 0:C
    // alejo 0:C
    // alejo 0:C 2:B
    // alejo 0:D

    CHECK_EQ(contents.size(), LineNumberDelta(5));

    contents.SplitLine(LineColumn(LineNumber(4), ColumnNumber(2)));
    // Contents:
    //
    // alejo 0:C
    // alejo 0:C
    // alejo 0:C 2:B
    // al 0:D
    // ejo 0:D

    CHECK_EQ(contents.size(), LineNumberDelta(6));
    {
      auto modifiers_4 = contents.at(LineNumber(4))->modifiers();
      CHECK_EQ(modifiers_4.size(), 1ul);
    }

    contents.FoldNextLine(LineNumber(4));
    // Contents:
    //
    // alejo 0:C
    // alejo 0:C
    // alejo 0:C 2:B
    // alejo 0:D

    CHECK_EQ(contents.size(), LineNumberDelta(5));
    {
      auto modifiers_4 = contents.at(LineNumber(4))->modifiers();
      for (auto& c : modifiers_4) {
        LOG(INFO) << "At: " << c.first << " " << *c.second.begin();
      }
      CHECK_EQ(modifiers_4.size(), 1ul);
    }
  }
}

void TestCursorsMove() {
  std::vector<CursorsTracker::Transformation> transformations;
  BufferContents contents([&](const CursorsTracker::Transformation& t) {
    transformations.push_back(t);
  });
  contents.set_line(LineNumber(0),
                    MakeNonNullShared<Line>(L"aleandro forero cuervo"));
  CHECK_EQ(transformations.size(), 0ul);
  contents.InsertCharacter(LineColumn(LineNumber(0), ColumnNumber(3)));
  CHECK_EQ(transformations.size(), 1ul);
  CHECK_EQ(transformations[0], CursorsTracker::Transformation());
  transformations.clear();

  contents.SetCharacter(LineColumn(LineNumber(0), ColumnNumber(3)), 'j', {});
  CHECK_EQ(transformations.size(), 1ul);
  transformations.clear();
}
}  // namespace

void BufferContentsTests() {
  LOG(INFO) << "BufferContents tests: start.";
  TestBufferContentsSnapshot();
  TestBufferInsertModifiers();
  TestCursorsMove();
  LOG(INFO) << "BufferContents tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
