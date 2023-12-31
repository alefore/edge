#ifndef __AFC_LANGUAGE_TEXT_LINE_H__
#define __AFC_LANGUAGE_TEXT_LINE_H__

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
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"

namespace afc::language::text {
struct LineMetadataEntry {
  language::lazy_string::LazyString initial_value;
  futures::ListenableValue<language::lazy_string::LazyString> value;
};

struct OutgoingLink {
  std::wstring path;
  std::optional<LineColumn> line_column;
};

class LineBuilder;

// This class is thread-safe.
class Line {
 public:
  Line() : Line(Line::Data{}) {}

  explicit Line(std::wstring text);

  Line(const Line& line);

  language::lazy_string::LazyString contents() const;
  language::lazy_string::ColumnNumber EndColumn() const;
  bool empty() const;

  wint_t get(language::lazy_string::ColumnNumber column) const;
  language::lazy_string::LazyString Substring(
      language::lazy_string::ColumnNumber column,
      language::lazy_string::ColumnNumberDelta length) const;

  // Returns the substring from pos to the end of the string.
  language::lazy_string::LazyString Substring(
      language::lazy_string::ColumnNumber column) const;

  std::wstring ToString() const { return contents().ToString(); }

  std::optional<language::lazy_string::LazyString> metadata() const;
  language::ValueOrError<
      futures::ListenableValue<language::lazy_string::LazyString>>
  metadata_future() const;

  const std::map<language::lazy_string::ColumnNumber,
                 afc::infrastructure::screen::LineModifierSet>&
  modifiers() const;

  afc::infrastructure::screen::LineModifierSet end_of_line_modifiers() const;

  std::function<void()> explicit_delete_observer() const;

  std::optional<OutgoingLink> outgoing_link() const;

  size_t hash() const { return hash_; }

  Line& operator=(const Line&) = default;

 private:
  struct Data {
    language::lazy_string::LazyString contents =
        language::lazy_string::LazyString();

    // Columns without an entry here reuse the last present value. If no
    // previous value, assume afc::infrastructure::screen::LineModifierSet().
    // There's no need to include RESET: it is assumed implicitly. In other
    // words, modifiers don't carry over past an entry.
    std::map<language::lazy_string::ColumnNumber,
             afc::infrastructure::screen::LineModifierSet>
        modifiers = {};

    // The semantics of this is that any characters at the end of the line
    // (i.e., the space that represents the end of the line) should be rendered
    // using these modifiers.
    //
    // If two lines are concatenated, the end of line modifiers of the first
    // line is entirely ignored; it doesn't affect the first characters from the
    // second line.
    afc::infrastructure::screen::LineModifierSet end_of_line_modifiers = {};

    std::optional<LineMetadataEntry> metadata = std::nullopt;
    std::function<void()> explicit_delete_observer = nullptr;
    std::optional<OutgoingLink> outgoing_link = std::nullopt;
  };

  friend class LineBuilder;

  explicit Line(Data data);
  static std::size_t ComputeHash(const Line::Data& data);

  language::NonNull<std::shared_ptr<const Data>> data_;
  size_t hash_;
};

std::ostream& operator<<(std::ostream& os, const afc::language::text::Line&);
}  // namespace afc::language::text
namespace std {
template <>
struct hash<afc::language::text::Line> {
  std::size_t operator()(const afc::language::text::Line& line) const {
    return line.hash();
  }
};

template <>
struct hash<afc::language::text::LineMetadataEntry> {
  std::size_t operator()(const afc::language::text::LineMetadataEntry& m) const;
};
}  // namespace std
#endif  // __AFC_LANGUAGE_TEXT_LINE_H__
