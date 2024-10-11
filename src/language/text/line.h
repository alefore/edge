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
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/cached_supplier.h"
#include "src/language/gc.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/vm/escape.h"

namespace afc::language::text {
struct LineMetadataKey
    : public GhostType<LineMetadataKey, lazy_string::SingleLine> {
  using GhostType::GhostType;
};

struct LineMetadataValue {
  lazy_string::SingleLine get_value() const;

  lazy_string::SingleLine initial_value;
  futures::ListenableValue<lazy_string::SingleLine> value;
};

struct OutgoingLink {
  infrastructure::Path path;
  std::optional<LineColumn> line_column;
};

class LineBuilder;

// This class is thread-safe.
class Line {
 public:
  Line() : Line(Line::Data{}) {}

  // TODO(2024-01-24): Get rid of this function.
  explicit Line(lazy_string::LazyString text);
  explicit Line(lazy_string::SingleLine text);
  explicit Line(lazy_string::NonEmptySingleLine text);

  Line(const Line& line);

  lazy_string::SingleLine contents() const;
  lazy_string::ColumnNumber EndColumn() const;
  bool empty() const;

  wchar_t get(lazy_string::ColumnNumber column) const;
  lazy_string::SingleLine Substring(
      lazy_string::ColumnNumber column,
      lazy_string::ColumnNumberDelta length) const;

  // Returns the substring from pos to the end of the string.
  lazy_string::SingleLine Substring(lazy_string::ColumnNumber column) const;

  std::wstring ToString() const { return contents().read().ToString(); }

  const std::map<LineMetadataKey, LineMetadataValue>& metadata() const;
  const std::map<lazy_string::ColumnNumber,
                 afc::infrastructure::screen::LineModifierSet>&
  modifiers() const;

  // Returns the modifiers that should be applied at a given column.
  afc::infrastructure::screen::LineModifierSet modifiers_at_position(
      lazy_string::ColumnNumber column) const;

  afc::infrastructure::screen::LineModifierSet end_of_line_modifiers() const;

  std::function<void()> explicit_delete_observer() const;

  std::optional<OutgoingLink> outgoing_link() const;

  size_t hash() const { return hash_; }

  const ValueOrError<vm::EscapedMap>& escaped_map() const;

  Line& operator=(const Line&) = default;
  bool operator==(const Line& a) const;
  bool operator<(const Line& other) const;

 private:
  struct Data {
    lazy_string::SingleLine contents = lazy_string::SingleLine{};

    // Columns without an entry here reuse the last present value. If no
    // previous value, assume afc::infrastructure::screen::LineModifierSet().
    // There's no need to include RESET: it is assumed implicitly. In other
    // words, modifiers don't carry over past an entry.
    std::map<lazy_string::ColumnNumber,
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

    std::map<LineMetadataKey, LineMetadataValue> metadata;
    std::function<void()> explicit_delete_observer = nullptr;
    std::optional<OutgoingLink> outgoing_link = std::nullopt;
    CachedSupplier<ValueOrError<vm::EscapedMap>> escaped_map_supplier =
        CachedSupplier<ValueOrError<vm::EscapedMap>>{[] {
          return language::Error{
              lazy_string::LazyString{L"No escaped map supplier."}};
        }};
  };

  friend class LineBuilder;

  explicit Line(Data data);
  static std::size_t ComputeHash(const Line::Data& data);

  language::NonNull<std::shared_ptr<const Data>> data_;
  size_t hash_;
};

lazy_string::LazyString ToLazyString(const Line& line);

std::ostream& operator<<(std::ostream& os, const Line&);
}  // namespace afc::language::text
namespace std {
template <>
struct hash<afc::language::text::Line> {
  std::size_t operator()(const afc::language::text::Line& line) const {
    return line.hash();
  }
};

template <>
struct hash<afc::language::text::LineMetadataValue> {
  std::size_t operator()(const afc::language::text::LineMetadataValue& m) const;
};
}  // namespace std
#endif  // __AFC_LANGUAGE_TEXT_LINE_H__
