#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

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

std::shared_ptr<LazyString> EmptyString();

}  // namespace afc::editor

#endif
