#ifndef __AFC_LANGUAGE_TEXT_LINE_BUILDER_H__
#define __AFC_LANGUAGE_TEXT_LINE_BUILDER_H__

#include <glog/logging.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"

namespace afc::language::text {

class LineBuilder {
 public:
  LineBuilder() : LineBuilder(language::lazy_string::LazyString()) {}

  explicit LineBuilder(const Line&);
  explicit LineBuilder(language::lazy_string::LazyString input_contents);

  LineBuilder(LineBuilder&&) = default;
  LineBuilder& operator=(LineBuilder&&) = default;

  // Use the explicit `Copy` method below.
  LineBuilder(const LineBuilder&) = delete;

  LineBuilder Copy() const;
  Line Build() &&;

  // Prefer `size`.
  language::lazy_string::ColumnNumber EndColumn() const;
  language::lazy_string::ColumnNumberDelta size() const;

  // Sets the character at the position given.
  //
  // `column` may be greater than size(), in which case the character will
  // just get appended (extending the line by exactly one character).
  void SetCharacter(
      language::lazy_string::ColumnNumber column, int c,
      const afc::infrastructure::screen::LineModifierSet& modifiers);

  void InsertCharacterAtPosition(language::lazy_string::ColumnNumber position);
  void AppendCharacter(wchar_t c,
                       afc::infrastructure::screen::LineModifierSet modifier);
  void AppendString(language::lazy_string::LazyString suffix);
  void AppendString(
      language::lazy_string::LazyString suffix,
      std::optional<afc::infrastructure::screen::LineModifierSet> modifier);

  // This function has linear complexity on the number of modifiers in `line`
  // and logarithmic on the length of `line` and `this`.
  void Append(LineBuilder line);

  void SetExplicitDeleteObserver(std::function<void()> observer) {
    data_.explicit_delete_observer = std::move(observer);
  }

  std::function<void()>& explicit_delete_observer() {
    return data_.explicit_delete_observer;
  }

  void SetOutgoingLink(OutgoingLink outgoing_link);
  std::optional<OutgoingLink> outgoing_link() const;

  LineBuilder& SetMetadata(
      std::map<lazy_string::LazyString, LineMetadataEntry> metadata);

  // Delete characters in [position, position + amount).
  LineBuilder& DeleteCharacters(
      language::lazy_string::ColumnNumber position,
      language::lazy_string::ColumnNumberDelta amount);

  // Delete characters from column (included) until the end.
  LineBuilder& DeleteSuffix(language::lazy_string::ColumnNumber column);

  LineBuilder& SetAllModifiers(
      afc::infrastructure::screen::LineModifierSet value);

  LineBuilder& insert_end_of_line_modifiers(
      afc::infrastructure::screen::LineModifierSet values);
  LineBuilder& set_end_of_line_modifiers(
      afc::infrastructure::screen::LineModifierSet values);
  afc::infrastructure::screen::LineModifierSet copy_end_of_line_modifiers()
      const;

  std::map<language::lazy_string::ColumnNumber,
           afc::infrastructure::screen::LineModifierSet>
  modifiers() const;
  size_t modifiers_size() const;
  bool modifiers_empty() const;
  std::pair<language::lazy_string::ColumnNumber,
            afc::infrastructure::screen::LineModifierSet>
  modifiers_last() const;
  void InsertModifier(language::lazy_string::ColumnNumber,
                      afc::infrastructure::screen::LineModifier);
  void InsertModifiers(language::lazy_string::ColumnNumber,
                       const afc::infrastructure::screen::LineModifierSet&);
  void set_modifiers(language::lazy_string::ColumnNumber,
                     afc::infrastructure::screen::LineModifierSet);
  void set_modifiers(std::map<language::lazy_string::ColumnNumber,
                              afc::infrastructure::screen::LineModifierSet>
                         value);
  void ClearModifiers();

  language::lazy_string::SingleLine contents() const;
  void set_contents(language::lazy_string::SingleLine);

 private:
  explicit LineBuilder(Line::Data);

  Line::Data data_;
  void ValidateInvariants();
};

}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_BUILDER_H__
