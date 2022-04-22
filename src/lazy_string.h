#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

#include "src/language/safe_types.h"

namespace afc::editor {
class ColumnNumber;
class ColumnNumberDelta;

// An immutable string. Implementations must ensure that methods always return
// the same values.
class LazyString {
 public:
  virtual ~LazyString() {}
  virtual wchar_t get(ColumnNumber pos) const = 0;
  virtual ColumnNumberDelta size() const = 0;

  std::wstring ToString() const;

  bool operator<(const LazyString& x);
};

language::NonNull<std::shared_ptr<LazyString>> EmptyString();
}  // namespace afc::editor

#endif
