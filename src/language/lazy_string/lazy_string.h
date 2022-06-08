#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"

namespace afc::editor {
GHOST_TYPE_NUMBER_WITH_DELTA(ColumnNumber, size_t, ColumnNumberDelta, int);

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

bool operator==(const LazyString& a, const LazyString& b);
}  // namespace afc::editor

GHOST_TYPE_TOP_LEVEL(afc::editor::ColumnNumber)
GHOST_TYPE_TOP_LEVEL(afc::editor::ColumnNumberDelta)

#endif
