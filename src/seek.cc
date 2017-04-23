#include "src/seek.h"

namespace afc {
namespace editor {

Seek::Seek(const OpenBuffer& buffer, LineColumn* position)
    : buffer_(buffer), position_(position) {}

Seek& Seek::WrappingLines() {
  wrapping_lines_ = true;
  return *this;
}

Seek& Seek::WithDirection(Direction direction) {
  direction_ = direction;
  return *this;
}

Seek& Seek::Backwards() {
  return WithDirection(BACKWARDS);
}

Seek::Result Seek::Once() const {
  return Advance(position_) ? DONE : UNABLE_TO_ADVANCE;
}

Seek::Result Seek::UntilCurrentCharIn(const wstring& word_char) const {
  while (word_char.find(CurrentChar()) == word_char.npos) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilCurrentCharNotIn(const wstring& word_char) const {
  while (word_char.find(CurrentChar()) != word_char.npos) {
    if (!Advance(position_)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilNextCharIn(const wstring& word_char) const {
  auto next_char = *position_;
  if (!Advance(&next_char)) { return UNABLE_TO_ADVANCE; }
  while (word_char.find(buffer_.character_at(next_char)) == word_char.npos) {
    *position_ = next_char;
    if (!Advance(&next_char)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

Seek::Result Seek::UntilNextCharNotIn(const wstring& word_char) const {
  auto next_char = *position_;
  if (!Advance(&next_char)) { return UNABLE_TO_ADVANCE; }
  while (word_char.find(buffer_.character_at(next_char)) != word_char.npos) {
    *position_ = next_char;
    if (!Advance(&next_char)) {
      return UNABLE_TO_ADVANCE;
    }
  }
  return DONE;
}

wchar_t Seek::CurrentChar() const {
  return buffer_.character_at(*position_);
}

bool Seek::Advance(LineColumn* position) const {
  switch (direction_) {
    case FORWARDS:
      if (buffer_.empty() || buffer_.at_end(*position)) {
        return false;
      } else if (!buffer_.at_end_of_line(*position)) {
        position->column ++;
      } else if (!wrapping_lines_) {
        return false;
      } else {
        position->line++;
        position->column = 0;
      }
      return true;

    case BACKWARDS:
      if (buffer_.empty() || *position == LineColumn()) {
        return false;
      } else if (position->column > 0) {
        position->column--;
      } else if (!wrapping_lines_) {
        return false;
      } else {
        position->line = min(position->line - 1, buffer_.lines_size() - 1);
        position->column = buffer_.LineAt(position->line)->size();
      }
      return true;
  }
  CHECK(false);
  return false;
}

}  // namespace editor
}  // namespace afc
