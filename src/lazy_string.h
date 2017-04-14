#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

namespace afc {
namespace editor {

using std::wstring;
using std::shared_ptr;

// An immutable string. Implementations must ensure that methods always return
// the same values.
class LazyString {
 public:
  virtual ~LazyString() {}
  virtual wchar_t get(size_t pos) const = 0;
  virtual size_t size() const = 0;

  wstring ToString() const {
    wstring output(size(), 0);
    for (size_t i = 0; i < output.size(); i++) {
      output.at(i) = get(i);
    }
    return output;
  }

  bool operator<(const LazyString& x) {
    for (size_t current = 0; current < size(); current++) {
      if (current == x.size()) {
        return false;
      }
      if (get(current) < x.get(current)) {
        return true;
      }
      if (get(current) > x.get(current)) {
        return false;
      }
    }
    return size() < x.size();
  }
};

shared_ptr<LazyString> EmptyString();

}  // namespace editor
}  // namespace afc

#endif
