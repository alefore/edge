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
  explicit LazyString(
      language::NonNull<std::shared_ptr<const LazyStringImpl>> data)
      : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const { return data_->get(pos); }
  ColumnNumberDelta size() const { return data_->size(); }

  std::wstring ToString() const;

  bool operator<(const LazyString& x);
};

LazyString EmptyString();

bool operator==(const LazyString& a, const LazyString& b);

std::ostream& operator<<(std::ostream& os,
                         const afc::language::lazy_string::LazyString& obj);
}  // namespace afc::language::lazy_string

GHOST_TYPE_TOP_LEVEL(afc::language::lazy_string::ColumnNumber)
GHOST_TYPE_TOP_LEVEL(afc::language::lazy_string::ColumnNumberDelta)

#endif  // __AFC_LANGUAGE_LAZY_STRING_LAZY_STRING_H__
