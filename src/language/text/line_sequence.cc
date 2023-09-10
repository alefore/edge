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

std::wstring LineSequence::ToString() const {
  std::wstring output;
  output.reserve(CountCharacters());
  EveryLine([&output](LineNumber position,
                      const NonNull<std::shared_ptr<const Line>>& line) {
    output.append((position == LineNumber(0) ? L"" : L"\n") + line->ToString());
    return true;
  });
  return output;
}

LineNumberDelta LineSequence::size() const {
  return LineNumberDelta(Lines::Size(lines_));
}

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

NonNull<std::shared_ptr<const Line>> LineSequence::at(
    LineNumber line_number) const {
  CHECK_LT(line_number, LineNumber(0) + size());
  return lines_->Get(line_number.read());
}

NonNull<std::shared_ptr<const Line>> LineSequence::back() const {
  CHECK(lines_ != nullptr);
  return at(EndLine());
}

NonNull<std::shared_ptr<const Line>> LineSequence::front() const {
  CHECK(lines_ != nullptr);
  return at(LineNumber(0));
}

bool LineSequence::EveryLine(
    const std::function<bool(LineNumber,
                             const NonNull<std::shared_ptr<const Line>>&)>&
        callback) const {
  LineNumber line_number;
  return Lines::Every(lines_,
                      [&](const NonNull<std::shared_ptr<const Line>>& line) {
                        return callback(line_number++, line);
                      });
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
