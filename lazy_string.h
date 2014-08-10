#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <string>

namespace afc {
namespace editor {

using std::string;
using std::shared_ptr;

class LazyString {
 public:
  virtual ~LazyString() {}
  virtual char get(size_t pos) const = 0;
  virtual size_t size() const = 0;

  string ToString() const {
    string output(size(), 0);
    for (int i = 0; i < output.size(); i++) {
      output.at(i) = get(i);
    }
    return output;
  }

  bool operator<(const LazyString& x) {
    for (int current = 0; current < size(); current++) {
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
