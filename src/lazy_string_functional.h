#ifndef __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__
#define __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__

#include <memory>
#include <string>

namespace afc {
namespace editor {

template <typename Function>
bool AllColumns(const LazyString& input, const Function& f) {
  for (ColumnNumber i; i < ColumnNumber(input.size()); ++i) {
    if (!f(i, input.get(i.column))) {
      return false;
    }
  }
  return true;
}

template <typename Function>
bool AnyColumn(const LazyString& input, const Function& f) {
  return !AllColumns(input, [f](LineColumn i, wchar_t c) { return !f(i, c); });
}

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__
