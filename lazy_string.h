#ifndef __AFC_EDITOR_LAZY_STRING_H__
#define __AFC_EDITOR_LAZY_STRING_H__

#include <memory>
#include <list>
#include <string>

namespace afc {
namespace editor {

class LazyString {
 public:
  virtual ~LazyString() {}
  virtual char get(size_t pos) = 0;
  virtual size_t size() = 0;
};

}  // namespace editor
}  // namespace afc

#endif
