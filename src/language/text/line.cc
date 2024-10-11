#include "src/language/text/line.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::compute_hash;
using afc::language::Error;
using afc::language::MakeHashableIteratorRange;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::language::text {

using ::operator<<;

Line::Line(LazyString contents) : Line(SingleLine{std::move(contents)}) {}

Line::Line(SingleLine contents)
    : Line(Data{.contents = std::move(contents), .metadata = {}}) {}

Line::Line(NonEmptySingleLine text) : Line(text.read()) {}

Line::Line(const Line& line)
    : data_(line.data_), hash_(ComputeHash(data_.value())) {}

/* static */
size_t Line::ComputeHash(const Line::Data& data) {
  return compute_hash(
      data.contents,
      MakeHashableIteratorRange(
          data.modifiers.begin(), data.modifiers.end(),
          [](const std::pair<ColumnNumber, LineModifierSet>& value) {
            return compute_hash(value.first,
                                MakeHashableIteratorRange(value.second));
          }),
      MakeHashableIteratorRange(data.end_of_line_modifiers),
      MakeHashableIteratorRange(
          data.metadata.begin(), data.metadata.end(),
          [](const std::pair<LineMetadataKey, LineMetadataValue>& value) {
            return compute_hash(value.first, value.second);
          }));
}

SingleLine Line::contents() const { return data_->contents; }

ColumnNumber Line::EndColumn() const {
  return ColumnNumber(0) + data_->contents.size();
}

bool Line::empty() const { return EndColumn().IsZero(); }

wchar_t Line::get(ColumnNumber column) const {
  CHECK_LT(column, EndColumn());
  return data_->contents.read().get(column);
}

SingleLine Line::Substring(ColumnNumber column, ColumnNumberDelta delta) const {
  return contents().Substring(column, delta);
}

SingleLine Line::Substring(ColumnNumber column) const {
  return contents().Substring(column);
}

const std::map<LineMetadataKey, LineMetadataValue>& Line::metadata() const {
  return data_->metadata;
}

SingleLine LineMetadataValue::get_value() const {
  return value.get_copy().value_or(initial_value);
}

const std::map<ColumnNumber, afc::infrastructure::screen::LineModifierSet>&
Line::modifiers() const {
  return data_->modifiers;
}

afc::infrastructure::screen::LineModifierSet Line::modifiers_at_position(
    ColumnNumber column) const {
  if (data_->modifiers.empty()) return LineModifierSet{};
  auto bound = data_->modifiers.lower_bound(column);
  if (bound != data_->modifiers.end() && bound->first == column)
    return bound->second;  // Exact match.
  if (bound == data_->modifiers.begin()) return LineModifierSet{};
  return std::prev(bound)->second;
}

afc::infrastructure::screen::LineModifierSet Line::end_of_line_modifiers()
    const {
  return data_->end_of_line_modifiers;
}

std::function<void()> Line::explicit_delete_observer() const {
  return data_->explicit_delete_observer;
}

std::optional<OutgoingLink> Line::outgoing_link() const {
  return data_->outgoing_link;
}

const ValueOrError<vm::EscapedMap>& Line::escaped_map() const {
  return data_->escaped_map_supplier();
}

Line::Line(Line::Data data)
    : data_(MakeNonNullShared<Line::Data>(std::move(data))),
      hash_(ComputeHash(data_.value())) {
  for (auto& m : data_->modifiers) {
    CHECK_LE(m.first, EndColumn()) << "Modifiers found past end of line.";
    CHECK(!m.second.contains(LineModifier::kReset));
  }
#if 0
  TRACK_OPERATION(Line_ValidateInvariants);
  ForEachColumn(Pointer(data_->contents).Reference(),
                [&contents = data_->contents](ColumnNumber, wchar_t c) {
                  CHECK(c != L'\n')
                      << "Line has newline character: " << contents->ToString();
                });
#endif
}

bool Line::operator==(const Line& a) const {
  return data_->contents == a.data_->contents &&
         data_->modifiers == a.data_->modifiers &&
         data_->end_of_line_modifiers == a.data_->end_of_line_modifiers;
}

bool Line::operator<(const Line& other) const {
  return contents() < other.contents();
}

lazy_string::LazyString ToLazyString(const Line& line) {
  return ToLazyString(line.contents());
}

std::ostream& operator<<(std::ostream& os, const Line& line) {
  os << line.contents();
  return os;
}

}  // namespace afc::language::text
namespace std {
std::size_t hash<afc::language::text::LineMetadataValue>::operator()(
    const afc::language::text::LineMetadataValue& m) const {
  return std::hash<SingleLine>{}(m.value.get_copy().value_or(m.initial_value));
}
}  // namespace std
