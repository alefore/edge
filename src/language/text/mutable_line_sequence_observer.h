#ifndef __AFC_LANGUAGE_TEXT_MUTABLE_LINE_SEQUENCE_OBSERVER_H__
#define __AFC_LANGUAGE_TEXT_MUTABLE_LINE_SEQUENCE_OBSERVER_H__

#include "src/language/text/line_column.h"

namespace afc::language::text {
class MutableLineSequenceObserver {
 public:
  virtual ~MutableLineSequenceObserver() = default;
  virtual void LinesInserted(language::text::LineNumber position,
                             language::text::LineNumberDelta size) = 0;
  virtual void LinesErased(language::text::LineNumber position,
                           language::text::LineNumberDelta size) = 0;
  virtual void SplitLine(language::text::LineColumn position) = 0;
  virtual void FoldedLine(language::text::LineColumn position) = 0;
  virtual void Sorted() = 0;
  virtual void AppendedToLine(language::text::LineColumn position) = 0;
  virtual void DeletedCharacters(
      language::text::LineColumn position,
      language::lazy_string::ColumnNumberDelta amount) = 0;
  virtual void SetCharacter(language::text::LineColumn position) = 0;
  virtual void InsertedCharacter(language::text::LineColumn position) = 0;
};

}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_MUTABLE_LINE_SEQUENCE_OBSERVER_H__
