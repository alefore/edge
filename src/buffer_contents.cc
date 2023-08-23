#include "src/buffer_contents.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/line.h"
#include "src/tests/tests.h"

namespace afc::editor {
using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewLazyString;

BufferContents::BufferContents() : BufferContents(UpdateListener()) {}

BufferContents::BufferContents(NonNull<std::shared_ptr<const Line>> line)
    : lines_(Lines::PushBack(nullptr, std::move(line))),
      update_listener_([](const CursorsTracker::Transformation&) {}) {}

BufferContents::BufferContents(UpdateListener update_listener)
    : update_listener_(update_listener != nullptr
                           ? std::move(update_listener)
                           : [](const CursorsTracker::Transformation&) {}) {
  CHECK(update_listener_ != nullptr);
}

LineNumber BufferContents::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range BufferContents::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back()->EndColumn()));
}

NonNull<std::unique_ptr<BufferContents>> BufferContents::copy() const {
  NonNull<std::unique_ptr<BufferContents>> output;
  output->lines_ = lines_;
  return output;
}

void BufferContents::FilterToRange(Range range) {
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

BufferContents BufferContentsForTests() {
  BufferContents output;
  output.AppendToLine(LineNumber(), Line(L"alejandro"));
  output.push_back(L"forero");
  output.push_back(L"cuervo");
  LOG(INFO) << "Contents: " << output.ToString();
  return output;
}

const bool filter_to_range_tests_registration = tests::Register(
    L"BufferContents::FilterToRange",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               BufferContents empty;
               empty.FilterToRange(Range());
               CHECK(empty.ToString() == L"");
             }},
        {.name = L"EmptyRange",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(Range());
               CHECK(buffer.ToString() == L"");
             }},
        {.name = L"WholeRange",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(buffer.range());
               CHECK(buffer.ToString() == BufferContentsForTests().ToString());
             }},
        {.name = L"FirstLineFewChars",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(0), ColumnNumber(3))});
               CHECK(buffer.ToString() == L"ale");
             }},
        {.name = L"FirstLineExcludingBreak",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(0), ColumnNumber(9))});
               CHECK(buffer.ToString() == L"alejandro");
             }},
        {.name = L"FirstLineIncludingBreak",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(Range{
                   LineColumn(), LineColumn(LineNumber(1), ColumnNumber(0))});
               CHECK(buffer.ToString() == L"alejandro\n");
             }},
        {.name = L"FirstLineMiddleChars",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                         LineColumn(LineNumber(0), ColumnNumber(5))});
               CHECK(buffer.ToString() == L"eja");
             }},
        {.name = L"MultiLineMiddle",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(3))});
               CHECK(buffer.ToString() == L"ejandro\nforero\ncue");
             }},
        {.name = L"LastLineFewChars",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.ToString() == L"ervo");
             }},
        {.name = L"LastLineExcludingBreak",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber()),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.ToString() == L"cuervo");
             }},
        {.name = L"LastLineIncludingBreak",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(1), ColumnNumber(6)),
                         LineColumn(LineNumber(2), ColumnNumber(6))});
               CHECK(buffer.ToString() == L"\ncuervo");
             }},
        {.name = L"LastLineMiddleChars",
         .callback =
             [] {
               auto buffer = BufferContentsForTests();
               buffer.FilterToRange(
                   Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                         LineColumn(LineNumber(2), ColumnNumber(5))});
               CHECK(buffer.ToString() == L"erv");
             }},
    });
}  // namespace

wint_t BufferContents::character_at(const LineColumn& position) const {
  CHECK_LE(position.line, EndLine());
  auto line = at(position.line);
  return position.column >= line->EndColumn() ? L'\n'
                                              : line->get(position.column);
}

LineColumn BufferContents::PositionBefore(LineColumn position) const {
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
    L"BufferContents::PositionBefore",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] { CHECK_EQ(BufferContents().PositionBefore({}), LineColumn()); }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(BufferContents().PositionBefore(
                         {LineNumber(), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContents().PositionBefore(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionBefore({}), LineColumn());
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(30)}),
                     LineColumn(LineNumber(),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber()}),
                     LineColumn(LineNumber(0),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber(4)}),
                     LineColumn(LineNumber(1), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferNormalLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionBefore(
                    {LineNumber(1), ColumnNumber(30)}),
                LineColumn(LineNumber(1), ColumnNumber(sizeof("forero") - 1)));
          }},
     {.name = L"NormalBufferLargeLineColumn", .callback = [] {
        CHECK_EQ(BufferContentsForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(10)}),
                 LineColumn(LineNumber(2), ColumnNumber(6)));
      }}});
}

LineColumn BufferContents::PositionAfter(LineColumn position) const {
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
    L"BufferContents::PositionAfter",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] { CHECK_EQ(BufferContents().PositionAfter({}), LineColumn()); }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(BufferContents().PositionAfter(
                         {LineNumber(0), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContents().PositionAfter(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter({}),
                     LineColumn(LineNumber(0), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(5)));
          }},
     {.name = L"NormalBufferZeroLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionAfter(
                    {LineNumber(0), ColumnNumber(sizeof("alejandro") - 1)}),
                LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(30)}),
                     LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(0)}),
                     LineColumn(LineNumber(1), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(3)}),
                     LineColumn(LineNumber(1), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferNormalLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(sizeof("forero") - 1)}),
                     LineColumn(LineNumber(2), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferEndLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(0)}),
                     LineColumn(LineNumber(2), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferEndLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(BufferContentsForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(3)}),
                     LineColumn(LineNumber(2), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferEndLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferEndLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(30)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(0)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(
                BufferContentsForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(3)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineLargeColumn", .callback = [] {
        CHECK_EQ(BufferContentsForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(30)}),
                 LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
      }}});
}

std::wstring BufferContents::ToString() const {
  std::wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](LineNumber position, const Line& line) {
    output.append((position == LineNumber(0) ? L"" : L"\n") + line.ToString());
    return true;
  });
  return output;
}

void BufferContents::insert(LineNumber position_line,
                            const BufferContents& source,
                            const std::optional<LineModifierSet>& modifiers) {
  CHECK_LE(position_line, EndLine());
  auto prefix = Lines::Prefix(lines_, position_line.read());
  auto suffix = Lines::Suffix(lines_, position_line.read());
  Lines::Every(source.lines_, [&](NonNull<std::shared_ptr<const Line>> line) {
    VLOG(6) << "Insert line: " << line->EndColumn() << " modifiers: "
            << (modifiers.has_value() ? modifiers->size() : -1);
    if (modifiers.has_value()) {
      LineBuilder builder(line.value());
      builder.SetAllModifiers(modifiers.value());
      line = MakeNonNullShared<Line>(std::move(builder).Build());
    }
    prefix = Lines::PushBack(prefix, line);
    return true;
  });
  lines_ = Lines::Append(prefix, suffix);
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(position_line))
                       .LineDelta(source.size()));
}

bool BufferContents::EveryLine(
    const std::function<bool(LineNumber, const Line&)>& callback) const {
  LineNumber line_number;
  return Lines::Every(lines_,
                      [&](const NonNull<std::shared_ptr<const Line>>& line) {
                        return callback(line_number++, line.value());
                      });
}

void BufferContents::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](LineNumber, const Line& line) {
    callback(line);
    return true;
  });
}

void BufferContents::ForEach(
    const std::function<void(std::wstring)>& callback) const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

size_t BufferContents::CountCharacters() const {
  ColumnNumberDelta output;
  ForEach([&output](const Line& line) {
    output += line.EndColumn().ToDelta() + ColumnNumberDelta(sizeof("\n") - 1);
  });
  if (output > ColumnNumberDelta(0)) {
    output--;  // Last line has no \n.
  }
  return output.read();
}

void BufferContents::insert_line(LineNumber line_position,
                                 NonNull<std::shared_ptr<const Line>> line) {
  LOG(INFO) << "Inserting line at position: " << line_position;
  size_t original_size = Lines::Size(lines_);
  auto prefix = Lines::Prefix(lines_, line_position.read());
  CHECK_EQ(Lines::Size(prefix), line_position.read());
  auto suffix = Lines::Suffix(lines_, line_position.read());
  CHECK_EQ(Lines::Size(suffix), Lines::Size(lines_) - line_position.read());
  lines_ = Lines::Append(Lines::PushBack(prefix, std::move(line)), suffix);
  CHECK_EQ(Lines::Size(lines_), original_size + 1);
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(line_position))
                       .LineDelta(LineNumberDelta(1)));
}

void BufferContents::set_line(LineNumber position,
                              NonNull<std::shared_ptr<const Line>> line) {
  static Tracker tracker(L"BufferContents::set_line");
  auto tracker_call = tracker.Call();

  if (position.ToDelta() >= size()) {
    return push_back(line);
  }

  lines_ = lines_->Replace(position.read(), std::move(line));
  // TODO: Why no notify update listeners?
}

void BufferContents::DeleteCharactersFromLine(LineColumn position,
                                              ColumnNumberDelta amount) {
  if (amount == ColumnNumberDelta(0)) {
    return;
  }
  CHECK_GT(amount, ColumnNumberDelta(0));
  CHECK_LE(position.column + amount, at(position.line)->EndColumn());

  TransformLine(
      position.line,
      [&](LineBuilder& options) {
        options.DeleteCharacters(position.column, amount);
      },
      CursorsTracker::Transformation()
          .WithBegin(position)
          .WithEnd(LineColumn(position.line + LineNumberDelta(1)))
          .ColumnDelta(-amount)
          .ColumnLowerBound(position.column));
}

void BufferContents::DeleteToLineEnd(LineColumn position) {
  if (position.column < at(position.line)->EndColumn()) {
    return DeleteCharactersFromLine(
        position, at(position.line)->EndColumn() - position.column);
  }
}

void BufferContents::SetCharacter(LineColumn position, int c,
                                  LineModifierSet modifiers) {
  VLOG(5) << "Set character: " << c << " at " << position
          << " with modifiers: " << modifiers.size();
  TransformLine(position.line, [&](LineBuilder& options) {
    options.SetCharacter(position.column, c, modifiers);
  });
}

void BufferContents::InsertCharacter(LineColumn position) {
  TransformLine(position.line, [&](LineBuilder& options) {
    options.InsertCharacterAtPosition(position.column);
  });
}

void BufferContents::AppendToLine(LineNumber position, Line line_to_append) {
  TransformLine(std::min(position, LineNumber() + size() - LineNumberDelta(1)),
                [&](LineBuilder& options) {
                  options.Append(LineBuilder(std::move(line_to_append)));
                });
}

void BufferContents::EraseLines(LineNumber first, LineNumber last,
                                CursorsBehavior cursors_behavior) {
  if (first == last) {
    return;  // Optimization to avoid notifying listeners.
  }
  CHECK_LE(first, last);
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
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(LineColumn(first))
                       .LineDelta(first - last)
                       .LineLowerBound(first));
}

void BufferContents::SplitLine(LineColumn position) {
  // TODO: Can maybe combine this with next for fewer updates.
  LineBuilder builder(at(position.line).value());
  builder.DeleteCharacters(ColumnNumber(0), position.column.ToDelta());
  insert_line(position.line + LineNumberDelta(1),
              MakeNonNullShared<Line>(std::move(builder).Build()));
  update_listener_(CursorsTracker::Transformation()
                       .WithBegin(position)
                       .WithEnd(LineColumn(position.line + LineNumberDelta(1)))
                       .LineDelta(LineNumberDelta(1))
                       .ColumnDelta(-position.column.ToDelta()));
  DeleteToLineEnd(position);
}

void BufferContents::FoldNextLine(LineNumber position) {
  auto next_line = position + LineNumberDelta(1);
  if (next_line.ToDelta() >= size()) {
    return;
  }

  ColumnNumberDelta initial_size = at(position)->EndColumn().ToDelta();
  // TODO: Can maybe combine this with next for fewer updates.
  AppendToLine(position, at(next_line).value());
  update_listener_(CursorsTracker::Transformation()
                       .WithLineEq(position + LineNumberDelta(1))
                       .LineDelta(LineNumberDelta(-1))
                       .ColumnDelta(initial_size));
  EraseLines(next_line, position + LineNumberDelta(2),
             CursorsBehavior::kAdjust);
}

void BufferContents::push_back(std::wstring str) {
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
    L"BufferContents::push_back(std::wstring)",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               BufferContents contents;
               contents.push_back(L"");
               CHECK(contents.ToString() == L"\n");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"SingleLine",
         .callback =
             [] {
               BufferContents contents;
               contents.push_back(L"foo");
               CHECK(contents.ToString() == L"\nfoo");
               CHECK_EQ(contents.EndLine(), LineNumber(1));
             }},
        {.name = L"MultiLine",
         .callback =
             [] {
               BufferContents contents;
               contents.push_back(L"foo\nbar\nhey\n\n\nquux");
               CHECK(contents.ToString() == L"\nfoo\nbar\nhey\n\n\nquux");
               CHECK_EQ(contents.EndLine(), LineNumber(6));
             }},
    });
}

void BufferContents::push_back(NonNull<std::shared_ptr<const Line>> line) {
  lines_ = Lines::PushBack(std::move(lines_), line);
  update_listener_({});
}

void BufferContents::append_back(
    std::vector<NonNull<std::shared_ptr<const Line>>> lines) {
  static Tracker tracker_subtree(L"BufferContents::append_back::subtree");
  auto tracker_subtree_call = tracker_subtree.Call();
  Lines::Ptr subtree = Lines::FromRange(lines.begin(), lines.end());
  tracker_subtree_call = nullptr;

  static Tracker tracker(L"BufferContents::append_back::append");
  auto tracker_call = tracker.Call();

  lines_ = Lines::Append(lines_, subtree);
  update_listener_({});
}

void BufferContents::SetUpdateListener(UpdateListener update_listener) {
  update_listener_ = std::move(update_listener);
}

LineColumn BufferContents::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(size(), LineNumberDelta(0));
  position.line = std::min(position.line, EndLine());
  position.column = std::min(at(position.line)->EndColumn(), position.column);
  return position;
}

std::vector<fuzz::Handler> BufferContents::FuzzHandlers() {
  using namespace fuzz;
  std::vector<Handler> output;

  // Call all our const methods that don't take any arguments.
  output.push_back(Call(std::function<void()>([this]() {
    size();
    EndLine();
    copy();
    back();
    front();
    ToString();
    CountCharacters();
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

}  // namespace afc::editor
