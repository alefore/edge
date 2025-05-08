#include "src/language/text/line_sequence.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;

namespace afc::language::text {

using ::operator<<;

/* static */ LineSequence LineSequence::BreakLines(LazyString input) {
  Lines::Ptr output = nullptr;
  ColumnNumber start;
  for (ColumnNumber i; i.ToDelta() < input.size(); ++i) {
    wchar_t c = input.get(i);
    CHECK_GE(i, start);
    if (c == '\n') {
      output =
          Lines::PushBack(std::move(output),
                          Line{SingleLine{input.Substring(start, i - start)}})
              .get_shared();
      start = i + ColumnNumberDelta(1);
    }
  }
  return LineSequence(Lines::PushBack(
      std::move(output), Line{SingleLine{LazyString{input.Substring(start)}}}));
}

/* static */ LineSequence LineSequence::ForTests(
    std::vector<std::wstring> inputs) {
  CHECK(!inputs.empty());
  Lines::Ptr output = nullptr;
  for (const std::wstring& input : inputs)
    output = Lines::PushBack(output, Line{SingleLine{LazyString{input}}})
                 .get_shared();
  // This is safe because we've validated that inputs isn't empty.
  return LineSequence(NonNull<Lines::Ptr>::Unsafe(std::move(output)));
}

/* static */ LineSequence LineSequence::WithLine(Line line) {
  return LineSequence(Lines::PushBack(nullptr, std::move(line)));
}

LineSequence LineSequence::ViewRange(Range range) const {
  CHECK_LE(range.end().line, EndLine());
  Lines::Ptr output = lines_.get_shared();

  output = Lines::Suffix(Lines::Prefix(output, range.end().line.read() + 1),
                         range.begin().line.read());

  if (range.end().column < output->Get(output->size() - 1).EndColumn()) {
    LineBuilder replacement(output->Get(output->size() - 1));
    replacement.DeleteSuffix(range.end().column);
    output = output->Replace(output->size() - 1, std::move(replacement).Build())
                 .get_shared();
  }

  if (!range.begin().column.IsZero()) {
    LineBuilder replacement(output->Get(0));
    replacement.DeleteCharacters(
        ColumnNumber(0),
        std::min(output->Get(0).EndColumn(), range.begin().column).ToDelta());
    output = output->Replace(0, std::move(replacement).Build()).get_shared();
  }

  return VisitPointer(
      output,
      [](NonNull<Lines::Ptr> lines) { return LineSequence(std::move(lines)); },
      [] { return LineSequence(Lines::PushBack(nullptr, Line())); });
}

namespace {
LineSequence LineSequenceForTests() {
  LineSequence output =
      LineSequence::ForTests({L"alejandro", L"forero", L"cuervo"});
  LOG(INFO) << "Contents: " << output.ToLazyString();
  return output;
}

const bool filter_to_range_tests_registration = tests::Register(
    L"LineSequence::ViewRange",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               CHECK_EQ(LineSequence().ViewRange(Range()).ToLazyString(),
                        LazyString{});
             }},
        {.name = L"EmptyRange",
         .callback =
             [] {
               CHECK_EQ(
                   LineSequenceForTests().ViewRange(Range()).ToLazyString(),
                   LazyString{});
             }},
        {.name = L"WholeRange",
         .callback =
             [] {
               LineSequence buffer = LineSequenceForTests();
               CHECK_EQ(buffer.ViewRange(buffer.range()).ToLazyString(),
                        buffer.ToLazyString());
             }},
        {.name = L"FirstLineFewChars",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(),
                                LineColumn(LineNumber(0), ColumnNumber(3))})
                            .ToLazyString(),
                        LazyString{L"ale"});
             }},
        {.name = L"FirstLineExcludingBreak",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(),
                                LineColumn(LineNumber(0), ColumnNumber(9))})
                            .ToLazyString(),
                        LazyString{L"alejandro"});
             }},
        {.name = L"FirstLineIncludingBreak",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(),
                                LineColumn(LineNumber(1), ColumnNumber(0))})
                            .ToLazyString(),
                        LazyString{L"alejandro\n"});
             }},
        {.name = L"FirstLineMiddleChars",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(0), ColumnNumber(2)),
                                LineColumn(LineNumber(0), ColumnNumber(5))})
                            .ToLazyString(),
                        LazyString{L"eja"});
             }},
        {.name = L"MultiLineMiddle",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(0), ColumnNumber(2)),
                                LineColumn(LineNumber(2), ColumnNumber(3))})
                            .ToLazyString(),
                        LazyString{L"ejandro\nforero\ncue"});
             }},
        {.name = L"LastLineFewChars",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(2), ColumnNumber(2)),
                                LineColumn(LineNumber(2), ColumnNumber(6))})
                            .ToLazyString(),
                        LazyString{L"ervo"});
             }},
        {.name = L"LastLineExcludingBreak",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(2), ColumnNumber()),
                                LineColumn(LineNumber(2), ColumnNumber(6))})
                            .ToLazyString(),
                        LazyString{L"cuervo"});
             }},
        {.name = L"LastLineIncludingBreak",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(1), ColumnNumber(6)),
                                LineColumn(LineNumber(2), ColumnNumber(6))})
                            .ToLazyString(),
                        LazyString{L"\ncuervo"});
             }},
        {.name = L"LastLineMiddleChars",
         .callback =
             [] {
               CHECK_EQ(LineSequenceForTests()
                            .ViewRange(Range{
                                LineColumn(LineNumber(2), ColumnNumber(2)),
                                LineColumn(LineNumber(2), ColumnNumber(5))})
                            .ToLazyString(),
                        LazyString{L"erv"});
             }},
    });
}  // namespace

std::wstring LineSequence::ToString() const {
  std::wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](LineNumber position, const Line& line) {
    output.append((position == LineNumber(0) ? L"" : L"\n") + line.ToString());
    return true;
  });
  VLOG(10) << "ToString: " << output;
  return output;
}

lazy_string::LazyString LineSequence::ToLazyString() const {
  // TODO(easy, 2023-09-11): Provide a more efficient implementation.
  return LazyString{ToString()};
}

lazy_string::SingleLine LineSequence::FoldLines() const {
  return Concatenate(*this | std::views::transform(&Line::contents) |
                     Intersperse(SingleLine{LazyString{L" "}}));
}

LineNumberDelta LineSequence::size() const {
  return LineNumberDelta(lines_->size());
}

LineNumber LineSequence::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range LineSequence::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back().EndColumn()));
}

size_t LineSequence::CountCharacters() const {
  ColumnNumberDelta output;
  ForEach([&output](const Line& line) {
    output += line.EndColumn().ToDelta() + ColumnNumberDelta(sizeof("\n") - 1);
  });
  if (output > ColumnNumberDelta(0)) {
    output--;  // Last line has no \n.
  }
  return output.read();
}

const Line& LineSequence::at(LineNumber line_number) const {
  CHECK_LT(line_number, LineNumber(0) + size());
  return lines_->Get(line_number.read());
}

const Line& LineSequence::back() const { return at(EndLine()); }

const Line& LineSequence::front() const { return at(LineNumber(0)); }

void LineSequence::ForEach(
    const std::function<void(const Line&)>& callback) const {
  EveryLine([callback](LineNumber, const Line& line) {
    callback(line);
    return true;
  });
}

void LineSequence::ForEach(
    const std::function<void(std::wstring)>& callback) const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

LineSequence LineSequence::Map(
    const std::function<Line(const Line&)>& transformer) const {
  return LineSequence(lines_->Map(transformer));
}

wint_t LineSequence::character_at(const LineColumn& position) const {
  CHECK_LE(position.line, EndLine());
  const Line& line = at(position.line);
  return position.column >= line.EndColumn() ? L'\n'
                                             : line.get(position.column);
}

LineColumn LineSequence::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(size(), LineNumberDelta(0));
  if (position.line > EndLine()) {
    position.line = EndLine();
    position.column = std::numeric_limits<ColumnNumber>::max();
  }
  position.column = std::min(at(position.line).EndColumn(), position.column);
  return position;
}

LineColumn LineSequence::PositionBefore(LineColumn position) const {
  if (position.line > EndLine()) {
    position.line = EndLine();
    position.column = at(position.line).EndColumn();
  } else if (position.column > at(position.line).EndColumn()) {
    position.column = at(position.line).EndColumn();
  } else if (position.column > ColumnNumber(0)) {
    position.column--;
  } else if (position.line > LineNumber(0)) {
    position.line =
        std::min(position.line, LineNumber(0) + size()) - LineNumberDelta(1);
    position.column = at(position.line).EndColumn();
  }
  return position;
}

namespace {
using ::operator<<;

const bool position_before_tests_registration = tests::Register(
    L"LineSequence::PositionBefore",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] { CHECK_EQ(LineSequence().PositionBefore({}), LineColumn()); }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(
                LineSequence().PositionBefore({LineNumber(), ColumnNumber(10)}),
                LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequence().PositionBefore(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionBefore({}), LineColumn());
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionBefore(
                         {LineNumber(), ColumnNumber(30)}),
                     LineColumn(LineNumber(),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber()}),
                     LineColumn(LineNumber(0),
                                ColumnNumber(sizeof("alejandro") - 1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionBefore(
                         {LineNumber(1), ColumnNumber(4)}),
                     LineColumn(LineNumber(1), ColumnNumber(3)));
          }},
     {.name = L"NormalBufferNormalLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionBefore(
                    {LineNumber(1), ColumnNumber(30)}),
                LineColumn(LineNumber(1), ColumnNumber(sizeof("forero") - 1)));
          }},
     {.name = L"NormalBufferLargeLineColumn", .callback = [] {
        CHECK_EQ(LineSequenceForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(10)}),
                 LineColumn(LineNumber(2), ColumnNumber(6)));
      }}});
}  // namespace

LineColumn LineSequence::PositionAfter(LineColumn position) const {
  if (position.line > EndLine()) {
    position.line = EndLine();
    position.column = at(position.line).EndColumn();
  } else if (position.column < at(position.line).EndColumn()) {
    ++position.column;
  } else if (position.line < EndLine()) {
    ++position.line;
    position.column = ColumnNumber();
  } else if (position.column > at(position.line).EndColumn()) {
    position.column = at(position.line).EndColumn();
  }
  return position;
}

LineSequenceIterator LineSequence::begin() const {
  return LineSequenceIterator(*this, LineNumber{});
}

LineSequenceIterator LineSequence::end() const {
  return LineSequenceIterator(*this, LineNumber{} + size());
}

bool LineSequence::operator==(const LineSequence& other) const {
  return std::equal(begin(), end(), other.begin(), other.end());
}

namespace {
const bool position_after_tests_registration = tests::Register(
    L"LineSequence::PositionAfter",
    {{.name = L"EmptyBufferZeroLineColumn",
      .callback =
          [] { CHECK_EQ(LineSequence().PositionAfter({}), LineColumn()); }},
     {.name = L"EmptyBufferZeroLine",
      .callback =
          [] {
            CHECK_EQ(
                LineSequence().PositionAfter({LineNumber(0), ColumnNumber(10)}),
                LineColumn());
          }},
     {.name = L"EmptyBufferNormalLineColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequence().PositionAfter(
                         {LineNumber(25), ColumnNumber(10)}),
                     LineColumn());
          }},
     {.name = L"NormalBufferZeroLineColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter({}),
                     LineColumn(LineNumber(0), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferZeroLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(4)}),
                     LineColumn(LineNumber(), ColumnNumber(5)));
          }},
     {.name = L"NormalBufferZeroLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionAfter(
                    {LineNumber(0), ColumnNumber(sizeof("alejandro") - 1)}),
                LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferZeroLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(0), ColumnNumber(30)}),
                     LineColumn(LineNumber(1), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferNormalLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(0)}),
                     LineColumn(LineNumber(1), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferNormalLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(3)}),
                     LineColumn(LineNumber(1), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferNormalLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(1), ColumnNumber(sizeof("forero") - 1)}),
                     LineColumn(LineNumber(2), ColumnNumber(0)));
          }},
     {.name = L"NormalBufferEndLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(0)}),
                     LineColumn(LineNumber(2), ColumnNumber(1)));
          }},
     {.name = L"NormalBufferEndLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(LineSequenceForTests().PositionAfter(
                         {LineNumber(2), ColumnNumber(3)}),
                     LineColumn(LineNumber(2), ColumnNumber(4)));
          }},
     {.name = L"NormalBufferEndLineEndColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferEndLineLargeColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionAfter(
                    {LineNumber(2), ColumnNumber(30)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineZeroColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(0)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineNormalColumn",
      .callback =
          [] {
            CHECK_EQ(
                LineSequenceForTests().PositionBefore(
                    {LineNumber(25), ColumnNumber(3)}),
                LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
          }},
     {.name = L"NormalBufferLargeLineLargeColumn", .callback = [] {
        CHECK_EQ(LineSequenceForTests().PositionBefore(
                     {LineNumber(25), ColumnNumber(30)}),
                 LineColumn(LineNumber(2), ColumnNumber(sizeof("cuervo") - 1)));
      }}});

const bool line_sequence_iterator_tests_registration = tests::Register(
    L"LineSequenceIterator", {{.name = L"EndSubtract", .callback = [] {
                                 LineSequence lines;
                                 CHECK_EQ(lines.end() - lines.end(), 0);
                               }}});

}  // namespace

LineSequenceIterator& LineSequenceIterator::operator--() {
  if (IsAtEnd()) {
    position_ = LineNumber{} + container_.size() - LineNumberDelta{1};
  } else {
    CHECK_GT(position_, LineNumber{});
    --position_;
  }
  return *this;
}

}  // namespace afc::language::text
