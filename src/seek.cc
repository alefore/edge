#include "src/seek.h"

#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"

#define ADVANCE_OR_RETURN(x) \
  if (!Advance(x)) return Result::kUnableToAdvance;

namespace afc::editor {
using language::lazy_string::ColumnNumber;
using language::text::Line;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineSequence;
using language::text::Range;

Seek::Seek(const LineSequence& contents, LineColumn* position)
    : contents_(contents),
      position_(position),
      range_(LineColumn(), LineColumn(contents_.EndLine().next())) {}

Seek& Seek::WrappingLines() {
  wrapping_lines_ = true;
  return *this;
}

Seek& Seek::WithDirection(Direction direction) {
  direction_ = direction;
  return *this;
}

Seek& Seek::Backwards() { return WithDirection(Direction::kBackwards); }

Seek& Seek::WithRange(Range range) {
  range_ = range;
  return *this;
}

Range Seek::range() const { return range_; }
bool Seek::AtRangeEnd() const { return *position_ >= range_.end(); }

wchar_t Seek::read() const { return contents_.character_at(*position_); }

Seek::Result Seek::Once() const {
  ADVANCE_OR_RETURN(position_);
  return Result::kDone;
}

Seek::Result Seek::ToNextLine() const {
  LineColumn next_position;
  if (direction_ == Direction::kForwards) {
    next_position.line = position_->line.next();
  } else {
    if (position_->line == LineNumber(0)) {
      return Result::kUnableToAdvance;
    }
    next_position.line = position_->line.previous();
    next_position.column = contents_.at(next_position.line).EndColumn();
  }

  if (!range_.Contains(next_position)) {
    return Result::kUnableToAdvance;
  }
  *position_ = next_position;
  return Result::kDone;
}

Seek::Result Seek::WhileCurrentCharIsUpper() const {
  return AdvanceWhile(iswupper);
}

Seek::Result Seek::WhileCurrentCharIsLower() const {
  return AdvanceWhile(iswlower);
}

Seek::Result Seek::UntilCurrentCharIsUpper() const {
  return AdvanceUntil(&iswupper);
}

Seek::Result Seek::UntilCurrentCharNotIsUpper() const {
  return AdvanceUntil(std::not_fn(&iswupper));
}

Seek::Result Seek::UntilCurrentCharIsAlpha() const {
  return AdvanceUntil(iswalpha);
}

Seek::Result Seek::UntilCurrentCharNotIsAlpha() const {
  return AdvanceUntil(std::not_fn(iswalpha));
}

Seek::Result Seek::UntilCurrentCharIn(
    const std::unordered_set<wchar_t>& word_char) const {
  CHECK_LE(position_->line, contents_.EndLine());
  return AdvanceUntil(
      [&word_char](wchar_t c) { return word_char.contains(c); });
}

Seek::Result Seek::UntilCurrentCharNotIn(
    const std::unordered_set<wchar_t>& word_char) const {
  return AdvanceUntil(
      [&word_char](wchar_t c) { return !word_char.contains(c); });
}

Seek::Result Seek::UntilNextCharIn(
    const std::unordered_set<wchar_t>& word_char) const {
  auto next_char = *position_;
  ADVANCE_OR_RETURN(&next_char);
  while (!word_char.contains(contents_.character_at(next_char))) {
    *position_ = next_char;
    ADVANCE_OR_RETURN(&next_char);
  }
  return Result::kDone;
}

Seek::Result Seek::UntilNextCharNotIn(
    const std::unordered_set<wchar_t>& word_char) const {
  auto next_char = *position_;
  ADVANCE_OR_RETURN(&next_char);
  while (word_char.contains(contents_.character_at(next_char))) {
    *position_ = next_char;
    ADVANCE_OR_RETURN(&next_char);
  }
  return Result::kDone;
}

Seek::Result Seek::ToEndOfLine() const {
  auto original_position = *position_;
  CHECK_LE(position_->line, contents_.EndLine());
  position_->column = contents_.at(position_->line).EndColumn();
  *position_ = std::min(range_.end(), *position_);
  return *position_ > original_position ? Result::kDone
                                        : Result::kUnableToAdvance;
}

Seek::Result Seek::UntilLine(
    std::function<bool(const Line& line)> predicate) const {
  bool advance = direction_ == Direction::kBackwards;
  while (true) {
    if (advance && !AdvanceLine(position_)) return Result::kUnableToAdvance;
    advance = true;

    if (predicate(contents_.at(position_->line))) {
      if (direction_ == Direction::kBackwards)
        position_->column = contents_.at(position_->line).EndColumn();
      return Result::kDone;
    }
  }
}

std::function<bool(const Line& line)> IsLineSubsetOf(
    const std::unordered_set<wchar_t>& allowed_chars) {
  return [allowed_chars](const Line& line) {
    return !FindFirstColumnWithPredicate(line.contents(), [&](ColumnNumber,
                                                              wchar_t c) {
              return !allowed_chars.contains(c);
            }).has_value();
  };
}

Seek::Result Seek::UntilNextLineIsSubsetOf(
    const std::unordered_set<wchar_t>& allowed_chars) const {
  return UntilLine(IsLineSubsetOf(allowed_chars));
}

Seek::Result Seek::UntilNextLineIsNotSubsetOf(
    const std::unordered_set<wchar_t>& allowed_chars) const {
  return UntilLine(std::not_fn(IsLineSubsetOf(allowed_chars)));
}

bool Seek::AdvanceLine(LineColumn* position) const {
  switch (direction_) {
    case Direction::kForwards:
      if (position->line.next() >= range_.end().line) {
        return false;
      }
      position->column = ColumnNumber(0);
      ++position->line;
      return true;

    case Direction::kBackwards:
      if (position->line == range_.begin().line) {
        return false;
      }
      position->column = ColumnNumber(0);
      position->line--;
      return true;
  }

  CHECK(false);
  return false;
}

bool Seek::Advance(LineColumn* position) const {
  switch (direction_) {
    case Direction::kForwards:
      if (*position >= range_.end()) {
        return false;
      } else if (position->column < contents_.at(position->line).EndColumn()) {
        ++position->column;
      } else if (!wrapping_lines_) {
        return false;
      } else if (LineColumn(position->line.next()) == range_.end()) {
        return false;
      } else {
        *position = LineColumn(position->line.next());
      }
      return true;

    case Direction::kBackwards:
      if (*position <= range_.begin()) {
        return false;
      } else if (position->column > ColumnNumber(0)) {
        --position->column;
      } else if (!wrapping_lines_) {
        return false;
      } else if (position->line == LineNumber(0)) {
        return false;
      } else {
        position->line =
            std::min(position->line.previous(), contents_.EndLine());
        position->column = contents_.at(position->line).EndColumn();
      }
      return true;
  }
  LOG(FATAL) << "Unhandled switch value.";
  ;
  return false;
}

}  // namespace afc::editor
