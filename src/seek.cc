#include "src/seek.h"

#include "src/language/wstring.h"
#include "src/lazy_string_functional.h"

namespace afc {
namespace editor {

Seek::Seek(const BufferContents& contents, LineColumn* position)
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
bool Seek::AtRangeEnd() const { return *position_ >= range_.end; }

wchar_t Seek::read() const { return contents_.character_at(*position_); }

Seek::Result Seek::Once() const {
  return Advance(position_) ? DONE : UNABLE_TO_ADVANCE;
}

Seek::Result Seek::ToNextLine() const {
  LineColumn next_position;
  if (direction_ == Direction::kForwards) {
    next_position.line = position_->line.next();
  } else {
    if (position_->line == LineNumber(0)) {
      return UNABLE_TO_ADVANCE;
    }
    next_position.line = position_->line.previous();
    next_position.column = contents_.at(next_position.line)->EndColumn();
  }

  if (!range_.Contains(next_position)) {
    return UNABLE_TO_ADVANCE;
  }
  *position_ = next_position;
  return DONE;
}

Seek::Result Seek::WhileCurrentCharIsUpper() const {
  while (iswupper(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::WhileCurrentCharIsLower() const {
  while (iswlower(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharIsUpper() const {
  while (!iswupper(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharNotIsUpper() const {
  while (iswupper(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharIsAlpha() const {
  while (!iswalpha(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharNotIsAlpha() const {
  while (iswalpha(read())) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharIn(const wstring& word_char) const {
  CHECK_LE(position_->line, contents_.EndLine());
  while (word_char.find(read()) == word_char.npos) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharNotIn(const wstring& word_char) const {
  while (word_char.find(read()) != word_char.npos) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilNextCharIn(const wstring& word_char) const {
  auto next_char = *position_;
  if (!Advance(&next_char)) {
    return UNABLE_TO_ADVANCE;
  }
  while (word_char.find(contents_.character_at(next_char)) == word_char.npos) {
    *position_ = next_char;
    if (!Advance(&next_char)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilNextCharNotIn(const wstring& word_char) const {
  auto next_char = *position_;
  if (!Advance(&next_char)) {
    return UNABLE_TO_ADVANCE;
  }
  while (word_char.find(contents_.character_at(next_char)) != word_char.npos) {
    *position_ = next_char;
    if (!Advance(&next_char)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::ToEndOfLine() const {
  auto original_position = *position_;
  CHECK_LE(position_->line, contents_.EndLine());
  position_->column = contents_.at(position_->line)->EndColumn();
  *position_ = std::min(range_.end, *position_);
  return *position_ > original_position ? DONE : UNABLE_TO_ADVANCE;
}

Seek::Result Seek::UntilLine(
    std::function<bool(const Line& line)> predicate) const {
  bool advance = direction_ == Direction::kBackwards;
  while (true) {
    if (advance && !AdvanceLine(position_)) {
      return UNABLE_TO_ADVANCE;
    }
    advance = true;

    if (predicate(*contents_.at(position_->line))) {
      if (direction_ == Direction::kBackwards) {
        position_->column = contents_.at(position_->line)->EndColumn();
      }
      return DONE;
    }
  }
}

std::function<bool(const Line& line)> Negate(
    std::function<bool(const Line& line)> predicate) {
  return [predicate](const Line& line) { return !predicate(line); };
}

std::function<bool(const Line& line)> IsLineSubsetOf(
    const wstring& allowed_chars) {
  return [allowed_chars](const Line& line) {
    return !FindFirstColumnWithPredicate(*line.contents(), [&](ColumnNumber,
                                                               wchar_t c) {
              return allowed_chars.find(c) == allowed_chars.npos;
            }).has_value();
  };
}

Seek::Result Seek::UntilNextLineIsSubsetOf(const wstring& allowed_chars) const {
  return UntilLine(IsLineSubsetOf(allowed_chars));
}

Seek::Result Seek::UntilNextLineIsNotSubsetOf(
    const wstring& allowed_chars) const {
  return UntilLine(Negate(IsLineSubsetOf(allowed_chars)));
}

bool Seek::AdvanceLine(LineColumn* position) const {
  switch (direction_) {
    case Direction::kForwards:
      if (position->line.next() >= range_.end.line) {
        return false;
      }
      position->column = ColumnNumber(0);
      ++position->line;
      return true;

    case Direction::kBackwards:
      if (position->line == range_.begin.line) {
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
      if (*position >= range_.end) {
        return false;
      } else if (position->column < contents_.at(position->line)->EndColumn()) {
        ++position->column;
      } else if (!wrapping_lines_) {
        return false;
      } else if (LineColumn(position->line.next()) == range_.end) {
        return false;
      } else {
        *position = LineColumn(position->line.next());
      }
      return true;

    case Direction::kBackwards:
      if (*position <= range_.begin) {
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
        position->column = contents_.at(position->line)->EndColumn();
      }
      return true;
  }
  LOG(FATAL) << "Unhandled switch value.";
  ;
  return false;
}

}  // namespace editor
}  // namespace afc
