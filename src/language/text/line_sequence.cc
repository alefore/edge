#include "src/language/text/line_sequence.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
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

using ::operator<<;

/* static */ LineSequence LineSequence::ForTests(
    std::vector<std::wstring> inputs) {
  CHECK(!inputs.empty());
  Lines::Ptr output = nullptr;
  for (const std::wstring& input : inputs) {
    output =
        Lines::PushBack(output, MakeNonNullShared<Line>(input)).get_shared();
  }
  // This is safe because we've validated that inputs isn't empty.
  return LineSequence(NonNull<Lines::Ptr>::Unsafe(std::move(output)));
}

/* static */ LineSequence LineSequence::WithLine(
    NonNull<std::shared_ptr<Line>> line) {
  return LineSequence(Lines::PushBack(nullptr, std::move(line)));
}

LineSequence LineSequence::ViewRange(Range range) const {
  CHECK_LE(range.end().line, EndLine());
  Lines::Ptr output = lines_.get_shared();

  output = Lines::Suffix(Lines::Prefix(output, range.end().line.read() + 1),
                         range.begin().line.read());

  if (range.end().column < output->Get(output->size() - 1)->EndColumn()) {
    LineBuilder replacement(output->Get(output->size() - 1).value());
    replacement.DeleteSuffix(range.end().column);
    output =
        output
            ->Replace(output->size() - 1,
                      MakeNonNullShared<Line>(std::move(replacement).Build()))
            .get_shared();
  }

  if (!range.begin().column.IsZero()) {
    LineBuilder replacement(output->Get(0).value());
    replacement.DeleteCharacters(
        ColumnNumber(0),
        std::min(output->Get(0)->EndColumn(), range.begin().column).ToDelta());
    output = output
                 ->Replace(
                     0, MakeNonNullShared<Line>(std::move(replacement).Build()))
                 .get_shared();
  }

  return VisitPointer(
      output,
      [](NonNull<Lines::Ptr> lines) { return LineSequence(std::move(lines)); },
      [] {
        return LineSequence(
            Lines::PushBack(nullptr, NonNull<std::shared_ptr<Line>>()));
      });
}

namespace {
LineSequence LineSequenceForTests() {
  LineSequence output =
      LineSequence::ForTests({L"alejandro", L"forero", L"cuervo"});
  LOG(INFO) << "Contents: " << output.ToString();
  return output;
}

const bool filter_to_range_tests_registration = tests::Register(
    L"LineSequence::ViewRange",
    {
        {.name = L"EmptyInput",
         .callback =
             [] {
               CHECK(LineSequence().ViewRange(Range()).ToString() == L"");
             }},
        {.name = L"EmptyRange",
         .callback =
             [] {
               CHECK(LineSequenceForTests().ViewRange(Range()).ToString() ==
                     L"");
             }},
        {.name = L"WholeRange",
         .callback =
             [] {
               LineSequence buffer = LineSequenceForTests();
               CHECK(buffer.ViewRange(buffer.range()).ToString() ==
                     buffer.ToString());
             }},
        {.name = L"FirstLineFewChars",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(),
                                   LineColumn(LineNumber(0), ColumnNumber(3))})
                         .ToString() == L"ale");
             }},
        {.name = L"FirstLineExcludingBreak",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(),
                                   LineColumn(LineNumber(0), ColumnNumber(9))})
                         .ToString() == L"alejandro");
             }},
        {.name = L"FirstLineIncludingBreak",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(),
                                   LineColumn(LineNumber(1), ColumnNumber(0))})
                         .ToString() == L"alejandro\n");
             }},
        {.name = L"FirstLineMiddleChars",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                                   LineColumn(LineNumber(0), ColumnNumber(5))})
                         .ToString() == L"eja");
             }},
        {.name = L"MultiLineMiddle",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(0), ColumnNumber(2)),
                                   LineColumn(LineNumber(2), ColumnNumber(3))})
                         .ToString() == L"ejandro\nforero\ncue");
             }},
        {.name = L"LastLineFewChars",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                                   LineColumn(LineNumber(2), ColumnNumber(6))})
                         .ToString() == L"ervo");
             }},
        {.name = L"LastLineExcludingBreak",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(2), ColumnNumber()),
                                   LineColumn(LineNumber(2), ColumnNumber(6))})
                         .ToString() == L"cuervo");
             }},
        {.name = L"LastLineIncludingBreak",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(1), ColumnNumber(6)),
                                   LineColumn(LineNumber(2), ColumnNumber(6))})
                         .ToString() == L"\ncuervo");
             }},
        {.name = L"LastLineMiddleChars",
         .callback =
             [] {
               CHECK(LineSequenceForTests()
                         .ViewRange(
                             Range{LineColumn(LineNumber(2), ColumnNumber(2)),
                                   LineColumn(LineNumber(2), ColumnNumber(5))})
                         .ToString() == L"erv");
             }},
    });
}  // namespace

std::wstring LineSequence::ToString() const {
  std::wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](LineNumber position,
                      const NonNull<std::shared_ptr<const Line>>& line) {
    output.append((position == LineNumber(0) ? L"" : L"\n") + line->ToString());
    return true;
  });
  VLOG(10) << "ToString: " << output;
  return output;
}

NonNull<std::shared_ptr<lazy_string::LazyString>> LineSequence::ToLazyString()
    const {
  // TODO(easy, 2023-09-11): Provide a more efficient implementation.
  return NewLazyString(ToString());
}

LineNumberDelta LineSequence::size() const {
  return LineNumberDelta(lines_->size());
}

bool LineSequence::empty() const { return size().IsZero(); }

LineNumber LineSequence::EndLine() const {
  return LineNumber(0) + size() - LineNumberDelta(1);
}

Range LineSequence::range() const {
  return Range(LineColumn(), LineColumn(EndLine(), back()->EndColumn()));
}

size_t LineSequence::CountCharacters() const {
  ColumnNumberDelta output;
  ForEach([&output](const NonNull<std::shared_ptr<const Line>>& line) {
    output += line->EndColumn().ToDelta() + ColumnNumberDelta(sizeof("\n") - 1);
  });
  if (output > ColumnNumberDelta(0)) {
    output--;  // Last line has no \n.
  }
  return output.read();
}

const NonNull<std::shared_ptr<const Line>>& LineSequence::at(
    LineNumber line_number) const {
  CHECK_LT(line_number, LineNumber(0) + size());
  return lines_->Get(line_number.read());
}

NonNull<std::shared_ptr<const Line>> LineSequence::back() const {
  return at(EndLine());
}

NonNull<std::shared_ptr<const Line>> LineSequence::front() const {
  return at(LineNumber(0));
}

bool LineSequence::ForEachLine(
    LineNumber start, LineNumberDelta length,
    const std::function<bool(
        LineNumber, const language::NonNull<std::shared_ptr<const Line>>&)>&
        callback) const {
  CHECK_GE(length, LineNumberDelta());
  CHECK_LE((start + length).ToDelta(), size());
  return Lines::Every(
      Lines::Suffix(Lines::Prefix(lines_.get_shared(), (start + length).read()),
                    start.read()),
      [&](const NonNull<std::shared_ptr<const Line>>& line) {
        return callback(start++, line);
      });
}

bool LineSequence::EveryLine(
    const std::function<bool(LineNumber,
                             const NonNull<std::shared_ptr<const Line>>&)>&
        callback) const {
  return ForEachLine(LineNumber(), size(), callback);
}

void LineSequence::ForEach(
    const std::function<void(const NonNull<std::shared_ptr<const Line>>&)>&
        callback) const {
  EveryLine(
      [callback](LineNumber, const NonNull<std::shared_ptr<const Line>>& line) {
        callback(line);
        return true;
      });
}

void LineSequence::ForEach(
    const std::function<void(std::wstring)>& callback) const {
  ForEach([callback](const NonNull<std::shared_ptr<const Line>>& line) {
    callback(line->ToString());
  });
}

wint_t LineSequence::character_at(const LineColumn& position) const {
  CHECK_LE(position.line, EndLine());
  auto line = at(position.line);
  return position.column >= line->EndColumn() ? L'\n'
                                              : line->get(position.column);
}

LineColumn LineSequence::AdjustLineColumn(LineColumn position) const {
  CHECK_GT(size(), LineNumberDelta(0));
  position.line = std::min(position.line, EndLine());
  position.column = std::min(at(position.line)->EndColumn(), position.column);
  return position;
}
}  // namespace afc::language::text
