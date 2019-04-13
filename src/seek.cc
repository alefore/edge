#include "src/seek.h"

#include "src/wstring.h"

namespace afc {
namespace editor {

Seek::Seek(const BufferContents& contents, LineColumn* position)
    : contents_(contents),
      position_(position),
      range_(LineColumn(), LineColumn(contents_.size(), 0)) {}

Seek& Seek::WrappingLines() {
  wrapping_lines_ = true;
  return *this;
}

Seek& Seek::WithDirection(Direction direction) {
  direction_ = direction;
  return *this;
}

Seek& Seek::Backwards() { return WithDirection(BACKWARDS); }

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
  CHECK_LT(position_->line, contents_.size());
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
  if (contents_.empty()) {
    *position_ = LineColumn();
  } else {
    position_->column = contents_.at(position_->line)->size();
    *position_ = std::min(range_.end, *position_);
  }
  return *position_ > original_position ? DONE : UNABLE_TO_ADVANCE;
}

Seek::Result Seek::UntilLine(
    std::function<bool(const Line& line)> predicate) const {
  bool advance = direction_ == BACKWARDS;
  while (true) {
    if (advance && !AdvanceLine(position_)) {
      return UNABLE_TO_ADVANCE;
    }
    advance = true;

    if (predicate(*contents_.at(position_->line))) {
      if (direction_ == BACKWARDS) {
        position_->column = contents_.at(position_->line)->size();
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
    for (size_t i = 0; i < line.size(); i++) {
      if (allowed_chars.find(line.get(i)) == allowed_chars.npos) {
        return false;
      }
    }
    return true;
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
    case FORWARDS:
      if (position->line + 1 >= range_.end.line) {
        return false;
      }
      position->column = 0;
      position->line++;
      return true;

    case BACKWARDS:
      if (position->line == range_.begin.line) {
        return false;
      }
      position->column = 0;
      position->line--;
      return true;
  }

  CHECK(false);
  return false;
}

bool Seek::Advance(LineColumn* position) const {
  switch (direction_) {
    case FORWARDS:
      if (contents_.empty() || *position >= range_.end) {
        return false;
      } else if (position->column < contents_.at(position->line)->size()) {
        position->column++;
      } else if (!wrapping_lines_) {
        return false;
      } else if (LineColumn(position->line + 1) == range_.end) {
        return false;
      } else {
        *position = LineColumn(position->line + 1);
      }
      return true;

    case BACKWARDS:
      if (contents_.empty() || *position <= range_.begin) {
        return false;
      } else if (position->column > 0) {
        position->column--;
      } else if (!wrapping_lines_) {
        return false;
      } else {
        position->line = std::min(position->line - 1, contents_.size() - 1);
        position->column = contents_.at(position->line)->size();
      }
      return true;
  }
  CHECK(false);
  return false;
}

}  // namespace editor
}  // namespace afc
