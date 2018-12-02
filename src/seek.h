#include "src/buffer.h"
#include "src/line_column.h"
#include "src/modifiers.h"

namespace afc {
namespace editor {

class Seek {
 public:
  Seek(const OpenBuffer& buffer, LineColumn* position);

  enum Result { DONE, UNABLE_TO_ADVANCE };

  Seek& WrappingLines();
  Seek& WithDirection(Direction direction);
  Seek& Backwards();
  Seek& WithRange(Range range);

  Result Once() const;
  Result UntilCurrentCharIn(const wstring& word_char) const;
  Result UntilCurrentCharNotIn(const wstring& word_char) const;
  Result UntilNextCharIn(const wstring& word_char) const;
  Result UntilNextCharNotIn(const wstring& word_char) const;
  Result UntilNextLineIsSubsetOf(const wstring& allowed_chars) const;
  Result UntilNextLineIsNotSubsetOf(const wstring& allowed_chars) const;
  Result UntilLine(std::function<bool(const Line& line)> predicate) const;

 private:
  wchar_t CurrentChar() const;
  bool Advance(LineColumn* position) const;
  bool AdvanceLine(LineColumn* position) const;

  const OpenBuffer& buffer_;
  LineColumn* const position_;

  bool wrapping_lines_ = false;
  Direction direction_ = FORWARDS;

  // Ensures that position will never move outside of this range.
  Range range_;
};

}  // namespace editor
}  // namespace afc
