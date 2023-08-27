#ifndef __AFC_EDITOR_LINE_H__
#define __AFC_EDITOR_LINE_H__

#include <glog/logging.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/observers.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"

namespace afc::editor {
struct LineMetadataEntry {
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
      initial_value;
  futures::ListenableValue<
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>>
      value;
};

struct OutgoingLink {
  std::wstring path;
  // TODO(trivial, 2023-08-27): Once line.h is in src/language/text, remove
  // the long namespace.
  std::optional<language::text::LineColumn> line_column;
};

class LineBuilder;

// TODO(2023-08-22, easy): Remove this dependency.
class LineWithCursor;

// This class is thread-safe.
class Line {
 public:
  Line() : Line(Line::Data{}) {}

  explicit Line(std::wstring text);
  Line(const Line& line);

  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  contents() const;
  language::lazy_string::ColumnNumber EndColumn() const;
  bool empty() const;

  wint_t get(language::lazy_string::ColumnNumber column) const;
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  Substring(language::lazy_string::ColumnNumber column,
            language::lazy_string::ColumnNumberDelta length) const;

  // Returns the substring from pos to the end of the string.
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  Substring(language::lazy_string::ColumnNumber column) const;

  std::wstring ToString() const { return contents()->ToString(); }

  std::shared_ptr<language::lazy_string::LazyString> metadata() const;
  language::ValueOrError<futures::ListenableValue<
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>>>
  metadata_future() const;

  std::map<language::lazy_string::ColumnNumber, LineModifierSet> modifiers()
      const {
    return data_.modifiers;
  }
  LineModifierSet end_of_line_modifiers() const {
    return data_.end_of_line_modifiers;
  }

  std::function<void()> explicit_delete_observer() const;

  std::optional<OutgoingLink> outgoing_link() const;

  size_t hash() const { return hash_; }

 private:
  struct Data {
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        contents = language::lazy_string::EmptyString();

    // Columns without an entry here reuse the last present value. If no
    // previous value, assume LineModifierSet(). There's no need to include
    // RESET: it is assumed implicitly. In other words, modifiers don't carry
    // over past an entry.
    std::map<language::lazy_string::ColumnNumber, LineModifierSet> modifiers =
        {};

    // The semantics of this is that any characters at the end of the line
    // (i.e., the space that represents the end of the line) should be rendered
    // using these modifiers.
    //
    // If two lines are concatenated, the end of line modifiers of the first
    // line is entirely ignored; it doesn't affect the first characters from the
    // second line.
    LineModifierSet end_of_line_modifiers = {};

    std::optional<LineMetadataEntry> metadata = std::nullopt;
    std::function<void()> explicit_delete_observer = nullptr;
    std::optional<OutgoingLink> outgoing_link = std::nullopt;
  };

  friend class std::hash<Line>;
  friend class LineBuilder;
  friend class LineWithCursor;

  explicit Line(Data data);
  static std::size_t ComputeHash(const Line::Data& data);

  // TODO(trivial, 2023-08-27): Get rid of this method.
  wint_t Get(language::lazy_string::ColumnNumber column) const;

  const Data data_;
  const size_t hash_;
};

class LineBuilder {
 public:
  LineBuilder() : LineBuilder(language::lazy_string::EmptyString()) {}

  explicit LineBuilder(Line&&);
  explicit LineBuilder(const Line&);
  explicit LineBuilder(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
          input_contents);

  LineBuilder(LineBuilder&&) = default;

  // Use the explicit `Copy` method below.
  LineBuilder(const LineBuilder&) = delete;

  LineBuilder Copy() const;
  Line Build() &&;

  language::lazy_string::ColumnNumber EndColumn() const;

  // Sets the character at the position given.
  //
  // `column` may be greater than size(), in which case the character will
  // just get appended (extending the line by exactly one character).
  void SetCharacter(language::lazy_string::ColumnNumber column, int c,
                    const LineModifierSet& modifiers);

  void InsertCharacterAtPosition(language::lazy_string::ColumnNumber position);
  void AppendCharacter(wchar_t c, LineModifierSet modifier);
  void AppendString(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
          suffix);
  void AppendString(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
          suffix,
      std::optional<LineModifierSet> modifier);
  void AppendString(std::wstring contents,
                    std::optional<LineModifierSet> modifier);
  void Append(LineBuilder line);

  void SetExplicitDeleteObserver(std::function<void()> observer) {
    data_.explicit_delete_observer = std::move(observer);
  }

  std::function<void()>& explicit_delete_observer() {
    return data_.explicit_delete_observer;
  }

  void SetOutgoingLink(OutgoingLink outgoing_link);
  std::optional<OutgoingLink> outgoing_link() const;

  LineBuilder& SetMetadata(std::optional<LineMetadataEntry> metadata);

  // Delete characters in [position, position + amount).
  LineBuilder& DeleteCharacters(
      language::lazy_string::ColumnNumber position,
      language::lazy_string::ColumnNumberDelta amount);

  // Delete characters from column (included) until the end.
  LineBuilder& DeleteSuffix(language::lazy_string::ColumnNumber column);

  LineBuilder& SetAllModifiers(LineModifierSet value);

  LineBuilder& insert_end_of_line_modifiers(LineModifierSet values);
  LineModifierSet copy_end_of_line_modifiers() const;

  std::map<language::lazy_string::ColumnNumber, LineModifierSet> modifiers()
      const;
  size_t modifiers_size() const;
  bool modifiers_empty() const;
  void InsertModifier(language::lazy_string::ColumnNumber, LineModifier);
  void set_modifiers(language::lazy_string::ColumnNumber, LineModifierSet);
  void set_modifiers(
      std::map<language::lazy_string::ColumnNumber, LineModifierSet> value);
  void ClearModifiers();

  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  contents() const;
  void set_contents(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>);

 private:
  friend LineWithCursor;
  // TODO(easy, 2023-08-21): Remove this friend. Add a `hash` method.
  friend class std::hash<Line>;

  explicit LineBuilder(Line::Data);

  Line::Data data_;
  void ValidateInvariants();
};
}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::Line> {
  std::size_t operator()(const afc::editor::Line& line) const {
    return line.hash();
  }
};

template <>
struct hash<afc::editor::LineMetadataEntry> {
  std::size_t operator()(const afc::editor::LineMetadataEntry&) const {
    // TODO(easy, 2023-08-27): Implement?
    return 0;
  }
};
}  // namespace std
#endif  // __AFC_EDITOR_LINE_H__
