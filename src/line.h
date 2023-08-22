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
#include "src/line_column.h"

namespace afc::editor {
class OpenBuffer;

struct LineMetadataEntry {
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
      initial_value;
  futures::ListenableValue<
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>>
      value;
};

struct BufferLineColumn {
  language::gc::WeakPtr<OpenBuffer> buffer;
  std::optional<LineColumn> position;
};

class LineBuilder;

// TODO(2023-08-22, easy): Remove this dependency.
class LineWithCursor;

// This class is thread-safe.
class Line {
 public:
  Line() : Line(Line::StableFields{}) {}

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
    return stable_fields_.modifiers;
  }
  LineModifierSet end_of_line_modifiers() const {
    return stable_fields_.end_of_line_modifiers;
  }

  bool modified() const {
    return data_.lock([=](const Data& data) { return data.modified; });
  }
  void set_modified(bool modified) {
    data_.lock([=](Data& data) { data.modified = modified; });
  }

  bool filtered() const {
    return data_.lock([](const Data& data) { return data.filtered; });
  }
  bool filter_version() const {
    return data_.lock([](const Data& data) { return data.filter_version; });
  }
  void set_filtered(bool filtered, size_t filter_version) {
    data_.lock([=](Data& data) {
      data.filtered = filtered;
      data.filter_version = filter_version;
    });
  }

  std::function<void()> explicit_delete_observer() const;

  std::optional<BufferLineColumn> buffer_line_column() const;

 private:
  struct StableFields {
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
    std::optional<BufferLineColumn> buffer_line_column = std::nullopt;
  };

  friend class std::hash<Line>;
  friend class LineBuilder;
  friend class LineWithCursor;

  explicit Line(StableFields stable_fields);

  struct Data {
    bool filtered = true;
    size_t filter_version = 0;
    bool modified = false;
    // This is mutable so that when it is computed (from a `const Data&`), we
    // can memoize the value.
    mutable std::optional<size_t> hash = std::nullopt;
  };

  static void ValidateInvariants(const Data& data);
  wint_t Get(language::lazy_string::ColumnNumber column) const;

  const StableFields stable_fields_;
  concurrent::Protected<Data, decltype(&Line::ValidateInvariants)> data_;
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

  void SetBufferLineColumn(BufferLineColumn buffer_line_column);
  std::optional<BufferLineColumn> buffer_line_column() const;

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

  explicit LineBuilder(Line::StableFields);

  Line::StableFields data_;
  void ValidateInvariants();
};
}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::Line> {
  std::size_t operator()(const afc::editor::Line& line) const;
};
}  // namespace std
#endif  // __AFC_EDITOR_LINE_H__
