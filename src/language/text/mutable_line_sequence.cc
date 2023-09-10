#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language::text {
using infrastructure::Tracker;
using infrastructure::screen::LineModifierSet;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewLazyString;

void NullMutableLineSequenceObserver::LinesInserted(LineNumber,
                                                    LineNumberDelta) {}
void NullMutableLineSequenceObserver::LinesErased(LineNumber, LineNumberDelta) {
}
void NullMutableLineSequenceObserver::SplitLine(LineColumn) {}
void NullMutableLineSequenceObserver::FoldedLine(LineColumn) {}
void NullMutableLineSequenceObserver::Sorted() {}
void NullMutableLineSequenceObserver::AppendedToLine(LineColumn) {}
void NullMutableLineSequenceObserver::DeletedCharacters(LineColumn,
                                                        ColumnNumberDelta) {}
void NullMutableLineSequenceObserver::SetCharacter(LineColumn) {}
void NullMutableLineSequenceObserver::InsertedCharacter(LineColumn) {}

MutableLineSequence::MutableLineSequence()
    : MutableLineSequence(
          MakeNonNullShared<NullMutableLineSequenceObserver>()) {}

/* static */ MutableLineSequence MutableLineSequence::WithLine(
    NonNull<std::shared_ptr<const Line>> line) {
  MutableLineSequence output;
  output.lines_ = Lines::PushBack(nullptr, std::move(line));
  return output;
}

MutableLineSequence::MutableLineSequence(
    NonNull<std::shared_ptr<MutableLineSequenceObserver>> observer)
    : observer_(std::move(observer)) {}

MutableLineSequence::MutableLineSequence(LineSequence lines)
    : lines_(std::move(lines.lines_)),
      observer_(MakeNonNullShared<NullMutableLineSequenceObserver>()) {}

LineSequence MutableLineSequence::snapshot() const {
  return LineSequence(lines_);
}

LineNumber MutableLineSequence::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range MutableLineSequence::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back()->EndColumn()));
}

NonNull<std::unique_ptr<MutableLineSequence>> MutableLineSequence::copy()
    const {
  NonNull<std::unique_ptr<MutableLineSequence>> output;
  output->lines_ = lines_;
  return output;
}

void MutableLineSequence::FilterToRange(Range range) {
  CHECK_LE(range.end.line, EndLine());
  // Drop the tail.
  if (range.end.line < EndLine()) {
    EraseLines(range.end.line + LineNumberDelta(1),
               EndLine() + LineNumberDelta(1), CursorsBehavior::kAdjust);
  }
  auto tail_line = at(range.end.line);
  range.end.column = std::min(range.end.column, tail_line->EndColumn());
  DeleteCharactersFromLine(range.end,
                           tail_line->EndColumn() - range.end.column);

  // Drop the head.
  range.begin.column =
      std::min(range.begin.column, at(range.begin.line)->EndColumn());
  if (range.begin.line > LineNumber()) {
    EraseLines(LineNumber(), range.begin.line, CursorsBehavior::kAdjust);
  }
  DeleteCharactersFromLine(LineColumn(), range.begin.column.ToDelta());
}

namespace {
using ::operator<<;

MutableLineSequence MutableLineSequenceForTests() {
  MutableLineSequence output;
  output.AppendToLine(LineNumber(), Line(L"alejandro"));
  output.push_back(L"forero");
  output.push_back(L"cuervo");
  LOG(INFO) << "Contents: " << output.snapshot().ToString();
  return output;
}

const bool filter_to_range_tests_registration = tests::Register(
    L"MutableLineSequence::FilterToRange",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               MutableLineSequence empty;
               empty.FilterToRange(Range());
               CHECK(empty.snapshot().ToString() == L"");
             }},
        {.name = L"EmptyRange",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(Range());
               CHECK(buffer.snapshot().ToString() == L"");
             }},
        {.name = L"WholeRange",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(buffer.range());
               CHECK(buffer.snapshot().ToString() ==
                     MutableLineSequenceForTests().snapshot().ToString());
             }},
        {.name = L"FirstLineFewChars",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(0), ColumnNumber(3))});
               CHECK(buffer.snapshot().ToString() == L"ale");
             }},
        {.name = L"FirstLineExcludingBreak",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(0), ColumnNumber(9))});
               CHECK(buffer.snapshot().ToString() == L"alejandro");
             }},
        {.name = L"FirstLineIncludingBreak",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(1), ColumnNumber(0))});
               CHECK(buffer.snapshot().ToString() == L"alejandro\n");
             }},
        {.name = L"FirstLineMiddleChars",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                         LineColumn(LineNumber(0), ColumnNumber(5))});
               CHECK(buffer.snapshot().ToString() == L"eja");
             }},
        {.name = L"MultiLineMiddle",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(3))});
               CHECK(buffer.snapshot().ToString() == L"ejandro\nforero\ncue");
             }},
        {.name = L"LastLineFewChars",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.snapshot().ToString() == L"ervo");
             }},
        {.name = L"LastLineExcludingBreak",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber()),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.snapshot().ToString() == L"cuervo");
             }},
        {.name = L"LastLineIncludingBreak",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(1), ColumnNumber(6)),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.snapshot().ToString() == L"\ncuervo");
             }},
        {.name = L"LastLineMiddleChars",
         .callback =
             [] {
               auto buffer = MutableLineSequenceForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(5))});
               CHECK(buffer.snapshot().ToString() == L"erv");
             }},
    });
}  // namespace

LineColumn MutableLineSequence::PositionBefore(LineColumn position) const {
  if (position.line > EndLine()) {
    position.line = EndLine();
    position.column = at(position.line)->EndColumn();
  } else if (position.column > at(position.line)->EndColumn()) {
    position.column = at(position.line)->EndColumn();
  } else if (position.column > ColumnNumber(0)) {
    position.column--;
  } else if (position.line > LineNumber(0)) {
    position.line =
        std::min(position.line, LineNumber(0) + size()) - LineNumberDelta(1);
    position.column = at(position.line)->EndColumn();
  }
  return position;
}

namespace {
const bool position_before_tests_registration = tests::Register(
    L"MutableLineSequence::PositionBefore",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionBefore({}), LineColumn());
          }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionBefore(
                         {LineNumber(), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionBefore(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionBefore({}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(30)}),
                     LineColumn(LineNumber(),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber()}),
                     LineColumn(LineNumber(0),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber(4)}),
                     LineColumn(LineNumber(1), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferNormalLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionBefore(
                    {LineNumber(1), ColumnNumber(30)}),
                LineColumn(LineNumber(1), ColumnNumber(sizeof("forero") - 1)));
          }},
     {.name = L"NormalBufferLargeLineColumn", .callback = [] {
        CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(10)}),
                 LineColumn(LineNumber(2), ColumnNumber(6)));
      }}});
}

LineColumn MutableLineSequence::PositionAfter(LineColumn position) const {
  if (position.line > EndLine()) {
    position.line = EndLine();
    position.column = at(position.line)->EndColumn();
  } else if (position.column < at(position.line)->EndColumn()) {
    ++position.column;
  } else if (position.line < EndLine()) {
    ++position.line;
    position.column = ColumnNumber();
  } else if (position.column > at(position.line)->EndColumn()) {
    position.column = at(position.line)->EndColumn();
  }
  return position;
}

namespace {
const bool position_after_tests_registration = tests::Register(
    L"MutableLineSequence::PositionAfter",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionAfter({}), LineColumn());
          }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionAfter(
                         {LineNumber(0), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequence().PositionAfter(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter({}),
                     LineColumn(LineNumber(0), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(5)));
          }},
     {.name = L"NormalBufferZeroLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionAfter(
                    {LineNumber(0), ColumnNumber(sizeof("alejandro") - 1)}),
                LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(30)}),
                     LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(0)}),
                     LineColumn(LineNumber(1), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(3)}),
                     LineColumn(LineNumber(1), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferNormalLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(sizeof("forero") - 1)}),
                     LineColumn(LineNumber(2), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferEndLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(0)}),
                     LineColumn(LineNumber(2), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferEndLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(MutableLineSequenceForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(3)}),
                     LineColumn(LineNumber(2), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferEndLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferEndLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(30)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(0)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(
                MutableLineSequenceForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(3)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineLargeColumn", .callback = [] {
        CHECK_EQ(MutableLineSequenceForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(30)}),
                 LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
      }}});
}

void MutableLineSequence::insert(
    LineNumber position_line, const LineSequence& source,
    const std::optional<LineModifierSet>& modifiers) {
  CHECK_LE(position_line, EndLine());
  auto prefix = Lines::Prefix(lines_, position_line.read());
  auto suffix = Lines::Suffix(lines_, position_line.read());
  source.ForEach([&](const NonNull<std::shared_ptr<const Line>>& line) {
    NonNull<std::shared_ptr<const Line>> line_to_insert = line;
    VLOG(6) << "Insert line: " << line->EndColumn() << " modifiers: "
            << (modifiers.has_value() ? modifiers->size() : -1);
    if (modifiers.has_value()) {
      LineBuilder builder(line_to_insert.value());
      builder.SetAllModifiers(modifiers.value());
      line_to_insert = MakeNonNullShared<Line>(std::move(builder).Build());
    }
    prefix = Lines::PushBack(prefix, line_to_insert);
    return true;
  });
  lines_ = Lines::Append(prefix, suffix);
  observer_->LinesInserted(position_line, source.size());
}

bool MutableLineSequence::EveryLine(
    const std::function<bool(LineNumber, const Line&)>& callback) const {
  LineNumber line_number;
  return Lines::Every(lines_,
                      [&](const NonNull<std::shared_ptr<const Line>>& line) {
                        return callback(line_number++, line.value());
                      });
}

void MutableLineSequence::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](LineNumber, const Line& line) {
    callback(line);
    return true;
  });
}

void MutableLineSequence::ForEach(
    const std::function<void(std::wstring)>& callback) const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

void MutableLineSequence::insert_line(LineNumber line_position,
                                      NonNull<std::shared_ptr<const Line>> line,
                                      CursorsBehavior cursors_behavior) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  size_t original_size = Lines::Size(lines_);
  auto prefix = Lines::Prefix(lines_, line_position.read());
  CHECK_EQ(Lines::Size(prefix), line_position.read());
  auto suffix = Lines::Suffix(lines_, line_position.read());
  CHECK_EQ(Lines::Size(suffix), Lines::Size(lines_) - line_position.read());
  lines_ = Lines::Append(Lines::PushBack(prefix, std::move(line)), suffix);
  CHECK_EQ(Lines::Size(lines_), original_size + 1);
  switch (cursors_behavior) {
    case CursorsBehavior::kUnmodified:
      break;
    case CursorsBehavior::kAdjust:
      observer_->LinesInserted(line_position, LineNumberDelta(1));
  }
}

void MutableLineSequence::set_line(LineNumber position,
                                   NonNull<std::shared_ptr<const Line>> line) {
  static Tracker tracker(L"MutableLineSequence::set_line");
  auto tracker_call = tracker.Call();

  if (position.ToDelta() >= size()) {
    return push_back(line);
  }

  lines_ = lines_->Replace(position.read(), std::move(line));
  // TODO: Why no notify observer_?
}

void MutableLineSequence::DeleteCharactersFromLine(
    LineColumn position, ColumnNumberDelta amount,
    CursorsBehavior cursors_behavior) {
  if (amount == ColumnNumberDelta(0)) {
    return;
  }
  CHECK_GT(amount, ColumnNumberDelta(0));
  CHECK_LE(position.column + amount, at(position.line)->EndColumn());

  TransformLine(position.line, [&](LineBuilder& options) {
    options.DeleteCharacters(position.column, amount);
  });

  switch (cursors_behavior) {
    case CursorsBehavior::kUnmodified:
      break;
    case CursorsBehavior::kAdjust:
      observer_->DeletedCharacters(position, amount);
  }
}

void MutableLineSequence::DeleteToLineEnd(LineColumn position,
                                          CursorsBehavior cursors_behavior) {
  if (position.column < at(position.line)->EndColumn()) {
    return DeleteCharactersFromLine(
        position, at(position.line)->EndColumn() - position.column,
        cursors_behavior);
  }
}

void MutableLineSequence::SetCharacter(LineColumn position, int c,
                                       LineModifierSet modifiers) {
  VLOG(5) << "Set character: " << c << " at " << position
          << " with modifiers: " << modifiers.size();
  TransformLine(position.line, [&](LineBuilder& options) {
    options.SetCharacter(position.column, c, modifiers);
  });

  observer_->SetCharacter(position);
}

void MutableLineSequence::InsertCharacter(LineColumn position) {
  TransformLine(position.line, [&](LineBuilder& options) {
    options.InsertCharacterAtPosition(position.column);
  });
  observer_->InsertedCharacter(position);
}

void MutableLineSequence::AppendToLine(LineNumber line, Line line_to_append,
                                       CursorsBehavior cursors_behavior) {
  const LineColumn position = LineColumn(
      std::min(line, EndLine()), at(std::min(line, EndLine()))->EndColumn());
  TransformLine(position.line, [&](LineBuilder& options) {
    options.Append(LineBuilder(std::move(line_to_append)));
  });
  switch (cursors_behavior) {
    case CursorsBehavior::kUnmodified:
      break;
    case CursorsBehavior::kAdjust:
      observer_->AppendedToLine(position);
  }
}

void MutableLineSequence::EraseLines(LineNumber first, LineNumber last,
                                     CursorsBehavior cursors_behavior) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LT(first, last);
  CHECK_LE(last, LineNumber(0) + size());
  LOG(INFO) << "Erasing lines in range [" << first << ", " << last << ").";
  lines_ = Lines::Append(Lines::Prefix(lines_, first.read()),
                         Lines::Suffix(lines_, last.read()));

  if (lines_ == nullptr) {
    lines_ = Lines::PushBack(nullptr, {});
  }

  if (cursors_behavior == CursorsBehavior::kUnmodified) {
    return;
  }
  observer_->LinesErased(first, last - first);
}

void MutableLineSequence::SplitLine(LineColumn position) {
  LineBuilder builder(at(position.line).value());
  builder.DeleteCharacters(ColumnNumber(0), position.column.ToDelta());
  insert_line(position.line + LineNumberDelta(1),
              MakeNonNullShared<Line>(std::move(builder).Build()),
              CursorsBehavior::kUnmodified);
  observer_->SplitLine(position);
  DeleteToLineEnd(position, CursorsBehavior::kUnmodified);
}

void MutableLineSequence::FoldNextLine(LineNumber position) {
  auto next_line = position + LineNumberDelta(1);
  if (next_line.ToDelta() >= size()) {
    return;
  }

  ColumnNumber initial_size = at(position)->EndColumn();
  AppendToLine(position, at(next_line).value(), CursorsBehavior::kUnmodified);
  EraseLines(next_line, position + LineNumberDelta(2),
             CursorsBehavior::kUnmodified);
  observer_->FoldedLine(LineColumn(position, initial_size));
}

void MutableLineSequence::push_back(std::wstring str) {
  ColumnNumber start;
  for (ColumnNumber i; i < ColumnNumber(str.size()); ++i) {
    wchar_t c = str[i.read()];
    CHECK_GE(i, start);
    if (c == '\n') {
      push_back(MakeNonNullShared<const Line>(
          str.substr(start.read(), (i - start).read())));
      start = i + ColumnNumberDelta(1);
    }
  }
  push_back(MakeNonNullShared<const Line>(str.substr(start.read())));
}

namespace {
const bool push_back_wstring_tests_registration = tests::Register(
    L"MutableLineSequence::push_back(std::wstring)",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"");
               CHECK(contents.snapshot().ToString() == L"\n");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"SingleLine",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"foo");
               CHECK(contents.snapshot().ToString() == L"\nfoo");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"MultiLine",
         .callback =
             [] {
               MutableLineSequence contents;
               contents.push_back(L"foo\nbar\nhey\n\n\nquux");
               CHECK(contents.snapshot().ToString() ==
                     L"\nfoo\nbar\nhey\n\n\nquux");
               CHECK_EQ(contents.EndLine(), LineNumber(6));
             }},
    });
}

void MutableLineSequence::push_back(NonNull<std::shared_ptr<const Line>> line) {
  LineNumber position = EndLine();
  lines_ = Lines::PushBack(std::move(lines_), line);
  observer_->LinesInserted(position + LineNumberDelta(1), LineNumberDelta(1));
}

void MutableLineSequence::append_back(
    std::vector<NonNull<std::shared_ptr<const Line>>> lines) {
  static Tracker tracker_subtree(L"MutableLineSequence::append_back::subtree");
  auto tracker_subtree_call = tracker_subtree.Call();
  Lines::Ptr subtree = Lines::FromRange(lines.begin(), lines.end());
  tracker_subtree_call = nullptr;

  static Tracker tracker(L"MutableLineSequence::append_back::append");
  auto tracker_call = tracker.Call();

  LineNumber position = EndLine();
  lines_ = Lines::Append(lines_, subtree);
  observer_->LinesInserted(position, LineNumberDelta(lines.size()));
}

LineColumn MutableLineSequence::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(size(), LineNumberDelta(0));
  position.line = std::min(position.line, EndLine());
  position.column = std::min(at(position.line)->EndColumn(), position.column);
  return position;
}

std::vector<tests::fuzz::Handler> MutableLineSequence::FuzzHandlers() {
  using namespace tests::fuzz;
  std::vector<Handler> output;

  // Call all our const methods that don't take any arguments.
  output.push_back(Call(std::function<void()>([this]() {
    size();
    EndLine();
    copy();
    back();
    front();
    snapshot().ToString();
    snapshot().CountCharacters();
  })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = LineNumber(line_number % size());
        insert_line(
            line_number,
            MakeNonNullShared<const Line>(
                LineBuilder(NewLazyString(std::move(text.value))).Build()));
      })));

  output.push_back(Call(std::function<void(LineNumber, ShortRandomLine)>(
      [this](LineNumber line_number, ShortRandomLine text) {
        line_number = LineNumber(line_number % size());
        set_line(
            line_number,
            MakeNonNullShared<const Line>(
                LineBuilder(NewLazyString(std::move(text.value))).Build()));
      })));

  // TODO: Call sort.
  // TODO: Call insert.
  // TODO: Call DeleteCharactersFromLine.
  // TODO: Call SetCharacter
  // TODO: Call InsertCharacter
  // TODO: Call AppendToLine.
  // TODO: Call EraseLines

  output.push_back(Call(std::function<void(LineNumber, LineNumber)>(
      [this](LineNumber a, LineNumber b) {
        a = LineNumber(a % size());
        b = LineNumber(b % size());
        EraseLines(std::min(a, b), std::max(a, b), CursorsBehavior::kAdjust);
      })));

  output.push_back(
      Call(std::function<void(LineColumn)>([this](LineColumn position) {
        position.line = LineNumber(position.line % size());
        auto line = at(position.line);
        if (line->empty()) {
          position.column = ColumnNumber(0);
        } else {
          position.column = ColumnNumber(position.column.ToDelta() %
                                         line->EndColumn().ToDelta());
        }
        SplitLine(position);
      })));

  output.push_back(
      Call(std::function<void(LineNumber)>([this](LineNumber line) {
        static const LineNumberDelta kMargin(10);
        // TODO: Declare a operator% for LineNumber and avoid the roundtrip.
        FoldNextLine(LineNumber(line % (size() + kMargin)));
      })));

  output.push_back(Call(std::function<void(ShortRandomLine)>(
      [this](ShortRandomLine s) { push_back(s.value); })));

  return output;
}

}  // namespace afc::language::text
