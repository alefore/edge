#ifndef __AFC_EDITOR_SEEK_H__
#define __AFC_EDITOR_SEEK_H__

#include "src/buffer_contents.h"
#include "src/direction.h"
#include "src/language/text/line_column.h"

namespace afc {
namespace editor {

class Seek {
 public:
  Seek(const BufferContents& contents, language::text::LineColumn* position);

  enum Result { DONE, UNABLE_TO_ADVANCE };

  Seek& WrappingLines();
  Seek& WithDirection(Direction direction);
  Seek& Backwards();
  Seek& WithRange(language::text::Range range);

  language::text::Range range() const;
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
  Result UntilCurrentCharIn(const std::wstring& word_char) const;
  Result UntilCurrentCharNotIn(const std::wstring& word_char) const;
  Result UntilNextCharIn(const std::wstring& word_char) const;
  Result UntilNextCharNotIn(const std::wstring& word_char) const;
  Result ToEndOfLine() const;
  Result UntilNextLineIsSubsetOf(const std::wstring& allowed_chars) const;
  Result UntilNextLineIsNotSubsetOf(const std::wstring& allowed_chars) const;
  Result UntilLine(std::function<bool(const Line& line)> predicate) const;

 private:
  bool Advance(language::text::LineColumn* position) const;
  bool AdvanceLine(language::text::LineColumn* position) const;

  const BufferContents& contents_;
  language::text::LineColumn* const position_;

  bool wrapping_lines_ = false;
  Direction direction_ = Direction::kForwards;

  // Ensures that position will never move outside of this range.
  language::text::Range range_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SEEK_H__
