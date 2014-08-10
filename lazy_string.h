#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <list>
#include <string>

namespace afc {
namespace editor {

using std::string;

class LazyString {
 public:
  virtual ~LazyString() {}
  virtual char get(size_t pos) const = 0;
  virtual size_t size() const = 0;

  string ToString() {
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

}  // namespace editor
}  // namespace afc

#endif
