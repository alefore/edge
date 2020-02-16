#ifndef __AFC_EDITOR_LAZY_STRING_APPEND_H__
#define __AFC_EDITOR_LAZY_STRING_APPEND_H__

#include <memory>
#include <vector>

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
std::shared_ptr<LazyString> Concatenate(
    std::vector<std::shared_ptr<LazyString>> inputs);

}  // namespace editor
}  // namespace afc

#endif
