#ifndef __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__
#define __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__

#include <memory>
#include <string>

#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
GHOST_TYPE_NUMBER_WITH_DELTA(ColumnNumber, size_t, ColumnNumberDelta, int);

// An immutable string. Implementations must ensure that identical calls to
// methods in a given instance always output the same values.
class LazyStringImpl {
 public:
  virtual ~LazyStringImpl() {}
  virtual wchar_t get(ColumnNumber pos) const = 0;
  virtual ColumnNumberDelta size() const = 0;
};

class AppendImpl;
class LazyStringImpl;

class LazyString {
  language::NonNull<std::shared_ptr<const LazyStringImpl>> data_;

  friend AppendImpl;
  friend LazyStringImpl;

 public:
  LazyString();

  explicit LazyString(std::wstring input);
  explicit LazyString(ColumnNumberDelta repetitions, wchar_t c);

  explicit LazyString(
      language::NonNull<std::shared_ptr<const LazyStringImpl>> data)
      : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const { return data_->get(pos); }
  ColumnNumberDelta size() const { return data_->size(); }
  bool IsEmpty() const { return data_->size().IsZero(); }

  std::wstring ToString() const;

  // Returns the substring from `column` to the end of the string.
  //
  // Equivalent to:
  //
  //     Substring(column, size() - column);
  //
  // LazyString{L"alejo"}.Substring(ColumnNumber{2}) => LazyString{L"ejo"}
  LazyString Substring(ColumnNumber column) const;

  // Returns the contents in [pos, pos + len).
  //
  // pos and len must be in the correct range (or else we'll crash).
  //
  // Example: LazyString{L"alejo"}.Substring(1, 2) := "le"
  LazyString Substring(ColumnNumber column, ColumnNumberDelta delta) const;

  // Similar to the other versions, but performs checks on the bounds; instead
  // of crashing on invalid bounds, returns a shorter string.
  //
  // Example: LazyString{L"alejo"}.SubstringWithRangeChecks(2, 30) := "ejo"
  LazyString SubstringWithRangeChecks(ColumnNumber column,
                                      ColumnNumberDelta delta) const;

  LazyString Append(LazyString) const;

  bool operator<(const LazyString& x) const;
};

bool operator==(const LazyString& a, const LazyString& b);
const LazyString& operator+=(LazyString& a, const LazyString& b);
LazyString operator+(const LazyString& a, const LazyString& b);

std::ostream& operator<<(std::ostream& os,
                         const afc::language::lazy_string::LazyString& obj);

std::wstring to_wstring(const LazyString&);
}  // namespace afc::language::lazy_string

GHOST_TYPE_TOP_LEVEL(afc::language::lazy_string::ColumnNumber)
GHOST_TYPE_TOP_LEVEL(afc::language::lazy_string::ColumnNumberDelta)

#endif  // __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__
