#ifndef __AFC_EDITOR_SEEK_H__
#define __AFC_EDITOR_SEEK_H__

#include "src/buffer_contents.h"
#include "src/line_column.h"
#include "src/modifiers.h"

namespace afc {
namespace editor {

class Seek {
 public:
  Seek(const BufferContents& contents, LineColumn* position);

  enum Result { DONE, UNABLE_TO_ADVANCE };

  Seek& WrappingLines();
  Seek& WithDirection(Direction direction);
  Seek& Backwards();
  Seek& WithRange(Range range);

  Range range() const;
  bool AtRangeEnd() const;

  wchar_t read() const;

  Result Once() const;
  // If seeking backwards, leaves the position at the end of the previous line.
  Result ToNextLine() const;
  Result WhileCurrentCharIsUpper() const;
  Result WhileCurrentCharIsLower() const;
  Result UntilCurrentCharIsUpper() const;
  Result UntilCurrentCharNotIsUpper() const;
  Result UntilCurrentCharIsAlpha() const;
  Result UntilCurrentCharNotIsAlpha() const;
  Result UntilCurrentCharIn(const wstring& word_char) const;
  Result UntilCurrentCharNotIn(const wstring& word_char) const;
  Result UntilNextCharIn(const wstring& word_char) const;
  Result UntilNextCharNotIn(const wstring& word_char) const;
  Result ToEndOfLine() const;
  Result UntilNextLineIsSubsetOf(const wstring& allowed_chars) const;
  Result UntilNextLineIsNotSubsetOf(const wstring& allowed_chars) const;
  Result UntilLine(std::function<bool(const Line& line)> predicate) const;

 private:
  bool Advance(LineColumn* position) const;
  bool AdvanceLine(LineColumn* position) const;

  const BufferContents& contents_;
  LineColumn* const position_;

  bool wrapping_lines_ = false;
  Direction direction_ = FORWARDS;

  // Ensures that position will never move outside of this range.
  Range range_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SEEK_H__
