#ifndef __AFC_EDITOR_LAZY_STRING_APPEND_H__
#define __AFC_EDITOR_LAZY_STRING_APPEND_H__

#include <memory>
#include "lazy_string.h"

namespace afc {
namespace editor {

std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b);
std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b,
                                         std::shared_ptr<LazyString> c);
std::shared_ptr<LazyString> StringAppend(std::shared_ptr<LazyString> a,
                                         std::shared_ptr<LazyString> b,
                                         std::shared_ptr<LazyString> c,
                                         std::shared_ptr<LazyString> d);

}  // namespace editor
}  // namespace afc

#endif
