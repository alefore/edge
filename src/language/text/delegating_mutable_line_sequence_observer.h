#ifndef __AFC_LANGUAGE_TEXT_DELEGATING_MUTABLE_LINE_SEQUENCE_OBSERVER_H__
#define __AFC_LANGUAGE_TEXT_DELEGATING_MUTABLE_LINE_SEQUENCE_OBSERVER_H__

#include "src/language/text/mutable_line_sequence.h"

namespace afc::language::text {
class DelegatingMutableLineSequenceObserver
    : public MutableLineSequenceObserver {
 public:
  using Delegate =
      language::NonNull<std::shared_ptr<MutableLineSequenceObserver>>;
  DelegatingMutableLineSequenceObserver(std::vector<Delegate> delegates);

  void LinesInserted(language::text::LineNumber position,
                     language::text::LineNumberDelta size) override;
  void LinesErased(language::text::LineNumber position,
                   language::text::LineNumberDelta size) override;
  void SplitLine(language::text::LineColumn position) override;
  void FoldedLine(language::text::LineColumn position) override;
  void Sorted() override;
  void AppendedToLine(language::text::LineColumn position) override;
  void DeletedCharacters(
      language::text::LineColumn position,
      language::lazy_string::ColumnNumberDelta amount) override;
  void SetCharacter(language::text::LineColumn position) override;
  void InsertedCharacter(language::text::LineColumn position) override;

 private:
  const std::vector<Delegate> delegates_;
};
}  // namespace afc::language::text

#endif  // __AFC_LANGUAGE_TEXT_DELEGATING_MUTABLE_LINE_SEQUENCE_OBSERVER_H__
