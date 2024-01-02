#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/infrastructure/screen/cursors.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/language/wstring.h"
#include "src/test/line_test.h"

namespace afc {
namespace editor {
namespace testing {
namespace {
using ::operator<<;
using infrastructure::screen::CursorsTracker;
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::ToByteString;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::MutableLineSequence;

void TestMutableLineSequenceSnapshot() {
  MutableLineSequence contents;
  for (auto& s : {L"alejandro", L"forero", L"cuervo"}) {
    contents.push_back(LineBuilder{LazyString{s}}.Build());
  }
  auto copy = contents.copy();
  CHECK_EQ("\nalejandro\nforero\ncuervo",
           ToByteString(contents.snapshot().ToString()));
  CHECK_EQ("\nalejandro\nforero\ncuervo",
           ToByteString(copy->snapshot().ToString()));

  contents.SplitLine(LineColumn(LineNumber(2), ColumnNumber(3)));
  CHECK_EQ("\nalejandro\nfor\nero\ncuervo",
           ToByteString(contents.snapshot().ToString()));
  CHECK_EQ("\nalejandro\nforero\ncuervo",
           ToByteString(copy->snapshot().ToString()));
}

void TestBufferInsertModifiers() {
  MutableLineSequence contents;
  LineBuilder options(LazyString{L"alejo"});
  options.set_modifiers(ColumnNumber(0), LineModifierSet{LineModifier::kCyan});

  contents.push_back(options.Copy().Build());  // LineNumber(1).
  contents.push_back(options.Copy().Build());  // LineNumber(2).
  options.set_modifiers(ColumnNumber(2), {LineModifier::kBold});
  contents.push_back(options.Copy().Build());  // LineNumber(3).
  LineBuilder new_line(contents.at(LineNumber(1)).value());
  new_line.SetAllModifiers(LineModifierSet({LineModifier::kDim}));
  contents.push_back(std::move(new_line).Build());  // LineNumber(4).

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
        LOG(INFO) << "At: " << c.first << " "
                  << ModifierToString(*c.second.begin());
      }
      CHECK_EQ(modifiers_4.size(), 1ul);
    }
  }
}

class TestObserver : public afc::language::text::MutableLineSequenceObserver {
 public:
  struct MessageLinesInserted {
    LineNumber position;
    LineNumberDelta size;
  };
  struct MessageLinesErased {
    LineNumber position;
    LineNumberDelta size;
  };
  struct MessageSplitLine {
    LineColumn position;
  };
  struct MessageFoldedLine {
    LineColumn position;
  };
  struct MessageSorted {};
  struct MessageAppendedToLine {
    LineColumn position;
  };
  struct MessageDeletedCharacters {
    LineColumn position;
    ColumnNumberDelta amount;
  };
  struct MessageSetCharacter {
    LineColumn position;
  };
  struct MessageInsertedCharacter {
    LineColumn position;
  };
  using Message =
      std::variant<MessageLinesInserted, MessageLinesErased, MessageSplitLine,
                   MessageFoldedLine, MessageSorted, MessageAppendedToLine,
                   MessageDeletedCharacters, MessageSetCharacter,
                   MessageInsertedCharacter>;

  TestObserver(std::vector<Message>& messages) : messages_(messages) {}

  void LinesInserted(LineNumber position, LineNumberDelta size) override {
    messages_.push_back(
        MessageLinesInserted{.position = position, .size = size});
  }
  void LinesErased(LineNumber position, LineNumberDelta size) override {
    messages_.push_back(MessageLinesErased{.position = position, .size = size});
  }
  void SplitLine(LineColumn position) override {
    messages_.push_back(MessageSplitLine{.position = position});
  }
  void FoldedLine(LineColumn position) override {
    messages_.push_back(MessageFoldedLine{.position = position});
  }
  void Sorted() override { messages_.push_back(MessageSorted{}); }
  void AppendedToLine(LineColumn position) override {
    messages_.push_back(MessageAppendedToLine{.position = position});
  }
  void DeletedCharacters(LineColumn position,
                         ColumnNumberDelta amount) override {
    messages_.push_back(
        MessageDeletedCharacters{.position = position, .amount = amount});
  }
  void SetCharacter(LineColumn position) override {
    messages_.push_back(MessageSetCharacter{.position = position});
  }
  void InsertedCharacter(LineColumn position) override {
    messages_.push_back(MessageInsertedCharacter{.position = position});
  }

 private:
  std::vector<Message>& messages_;
};

void TestCursorsMove() {
  std::vector<TestObserver::Message> messages;
  MutableLineSequence contents(MakeNonNullUnique<TestObserver>(messages));
  contents.set_line(LineNumber(0), Line(L"aleandro forero cuervo"));
  CHECK_EQ(messages.size(), 0ul);
  contents.InsertCharacter(LineColumn(LineNumber(0), ColumnNumber(3)));
  CHECK_EQ(messages.size(), 1ul);
  CHECK_EQ(
      std::get<TestObserver::MessageInsertedCharacter>(messages[0]).position,
      LineColumn(LineNumber(0), ColumnNumber(3)));
  messages.clear();

  contents.SetCharacter(LineColumn(LineNumber(0), ColumnNumber(2)), 'j', {});
  CHECK_EQ(messages.size(), 1ul);
  CHECK_EQ(std::get<TestObserver::MessageSetCharacter>(messages[0]).position,
           LineColumn(LineNumber(0), ColumnNumber(2)));
}
}  // namespace

void MutableLineSequenceTests() {
  LOG(INFO) << "MutableLineSequence tests: start.";
  TestMutableLineSequenceSnapshot();
  TestBufferInsertModifiers();

  TestCursorsMove();
  LOG(INFO) << "MutableLineSequence tests: done.";
}

}  // namespace testing
}  // namespace editor
}  // namespace afc
