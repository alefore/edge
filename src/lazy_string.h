#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

namespace afc {
namespace editor {

using std::shared_ptr;
using std::wstring;

class ColumnNumber;

// An immutable string. Implementations must ensure that methods always return
// the same values.
class LazyString {
 public:
  virtual ~LazyString() {}
  virtual wchar_t get(ColumnNumber pos) const = 0;
  virtual size_t size() const = 0;

  wstring ToString() const;

  bool operator<(const LazyString& x);
};

shared_ptr<LazyString> EmptyString();

}  // namespace editor
}  // namespace afc

#endif
