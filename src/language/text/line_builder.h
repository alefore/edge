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
  LineBuilder() : LineBuilder(language::lazy_string::SingleLine()) {}

  explicit LineBuilder(const Line&);
  explicit LineBuilder(language::lazy_string::SingleLine input_contents);
  LineBuilder(language::lazy_string::SingleLine input_contents,
              LinePartMetadata modifiers);
  explicit LineBuilder(
      language::lazy_string::NonEmptySingleLine input_contents);

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
  void SetCharacter(language::lazy_string::ColumnNumber column, int c,
                    const LinePartMetadata& modifiers);

  void InsertCharacterAtPosition(language::lazy_string::ColumnNumber position);
  void AppendCharacter(wchar_t c, LinePartMetadata modifier);
  void AppendString(language::lazy_string::SingleLine suffix);
  void AppendString(language::lazy_string::SingleLine suffix,
                    std::optional<LinePartMetadata> modifier);

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

  template <typename Self>
  decltype(auto) SetMetadata(this Self&& self,
                             language::LazyValue<LineMetadataMap> metadata) {
    self.InternalSetMetadata(std::move(metadata));
    return std::forward<Self>(self);
  }

  // Delete characters in [position, position + amount).
  LineBuilder& DeleteCharacters(
      language::lazy_string::ColumnNumber position,
      language::lazy_string::ColumnNumberDelta amount);

  // Delete characters from column (included) until the end.
  LineBuilder& DeleteSuffix(language::lazy_string::ColumnNumber column);

  LineBuilder& SetAllModifiers(LinePartMetadata value);

  LineBuilder& insert_end_of_line_modifiers(LinePartMetadata values);
  LineBuilder& set_end_of_line_modifiers(LinePartMetadata values);
  LinePartMetadata copy_end_of_line_modifiers() const;

  // TODO(2026-05-07, P1, rename, trivial): Rename `modifiers` to
  // `line_part_metadata_map`. Same for `modifiers_size` and `modifiers_empty`
  // and `modifiers_last`.
  LinePartMetadataMap modifiers() const;
  size_t modifiers_size() const;
  bool modifiers_empty() const;
  std::pair<language::lazy_string::ColumnNumber, LinePartMetadata>
  modifiers_last() const;
  void InsertModifiers(language::lazy_string::ColumnNumber,
                       const LinePartMetadata&);
  void set_modifiers(language::lazy_string::ColumnNumber, LinePartMetadata);
  void set_modifiers(LinePartMetadataMap value);
  void ClearModifiers();

  language::lazy_string::SingleLine contents() const;
  void set_contents(language::lazy_string::SingleLine);

 private:
  explicit LineBuilder(Line::Data);

  void InternalSetMetadata(language::LazyValue<LineMetadataMap> metadata);

  Line::Data data_;
  void ValidateInvariants();
};

}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_BUILDER_H__
