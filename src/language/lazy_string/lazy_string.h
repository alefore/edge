#ifndef __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__
#define __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__

#include <memory>
#include <string>

#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
class ColumnNumber;
class ColumnNumberDelta;

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
class LazyStringIterator;

class LazyString {
  language::NonNull<std::shared_ptr<const LazyStringImpl>> data_;

  friend AppendImpl;
  friend LazyStringImpl;
  friend LazyStringIterator;

 public:
  LazyString();

  explicit LazyString(std::wstring input);
  explicit LazyString(ColumnNumberDelta repetitions, wchar_t c);

  explicit LazyString(
      language::NonNull<std::shared_ptr<const LazyStringImpl>> data)
      : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const;
  ColumnNumberDelta size() const;
  bool empty() const;

  std::wstring ToString() const;

  LazyStringIterator begin() const;
  LazyStringIterator end() const;

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

  std::string ToBytes() const;

  std::strong_ordering operator<=>(const LazyString& x) const;
  bool operator==(const LazyString& other) const;
};

const LazyString& operator+=(LazyString& a, const LazyString& b);
LazyString operator+(const LazyString& a, const LazyString& b);

std::ostream& operator<<(std::ostream& os,
                         const afc::language::lazy_string::LazyString& obj);

std::wstring to_wstring(const LazyString&);

template <typename T>
  requires std::same_as<
      typename std::remove_const<typename std::remove_reference<T>::type>::type,
      LazyString>
LazyString ToLazyString(T obj) {
  return obj;
}

template <typename T>
  requires(!std::same_as<typename std::remove_const<
                             typename std::remove_reference<T>::type>::type,
                         LazyString>)
LazyString ToLazyString(T obj) {
  return ToLazyString(obj.read());
}

template <typename I>
concept ConvertibleToLazyString =
    !std::same_as<I, LazyString> &&
    // We exclude std::optional<LazyString> explicitly, to avoid ambiguity with
    // the operator== declared for std::optional<> types.
    !std::same_as<I, std::optional<LazyString>> && requires(I i) {
      { ToLazyString(i) } -> std::same_as<LazyString>;
    };

template <typename I>
  requires ConvertibleToLazyString<I>
bool operator==(const I& i, const LazyString& other) {
  return ToLazyString(i) == other;
}

template <typename I>
  requires ConvertibleToLazyString<I>
const LazyString& operator+=(LazyString& a, const I& b) {
  return a += ToLazyString(b);
}

template <typename I>
  requires ConvertibleToLazyString<I>
LazyString operator+(const LazyString& a, const I& b) {
  return a + ToLazyString(b);
}

template <typename I>
  requires ConvertibleToLazyString<I>
LazyString operator+(const I& a, const LazyString& b) {
  return ToLazyString(a) + b;
}

}  // namespace afc::language::lazy_string
#include "src/language/lazy_string/column_number.h"

namespace afc::language::lazy_string {

class LazyStringIterator {
 private:
  LazyString container_;
  ColumnNumber position_;

 public:
  LazyStringIterator() {}
  LazyStringIterator(const LazyStringIterator&) = default;
  LazyStringIterator(LazyStringIterator&&) = default;
  LazyStringIterator& operator=(const LazyStringIterator&) = default;
  LazyStringIterator& operator=(LazyStringIterator&&) = default;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = wchar_t;
  using reference = value_type&;

  LazyStringIterator(LazyString container, ColumnNumber position)
      : container_(std::move(container)), position_(position) {}

  wchar_t operator*() const { return container_.get(position_); }

  bool operator!=(const LazyStringIterator& other) const;
  bool operator==(const LazyStringIterator& other) const;
  LazyStringIterator& operator++();    // Prefix increment.
  LazyStringIterator operator++(int);  // Postfix increment.
  int operator-(const LazyStringIterator& other) const;
  LazyStringIterator operator+(int n) const;
  LazyStringIterator operator+(int n);

 private:
  bool IsAtEnd() const;
};

}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__
