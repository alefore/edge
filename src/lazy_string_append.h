#ifndef __AFC_EDITOR_LAZY_STRING_APPEND_H__
#define __AFC_EDITOR_LAZY_STRING_APPEND_H__

#include <memory>
#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;

// TODO: Pass by value.
std::shared_ptr<LazyString> StringAppend(const shared_ptr<LazyString>& a,
                                         const shared_ptr<LazyString>& b);
std::shared_ptr<LazyString> StringAppend(const shared_ptr<LazyString>& a,
                                         const shared_ptr<LazyString>& b,
                                         const shared_ptr<LazyString>& c);

}  // namespace editor
}  // namespace afc

#endif
