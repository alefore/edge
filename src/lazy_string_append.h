#ifndef __AFC_EDITOR_LAZY_STRING_APPEND_H__
#define __AFC_EDITOR_LAZY_STRING_APPEND_H__

#include <memory>
#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;

std::shared_ptr<LazyString> StringAppend(const shared_ptr<LazyString>& a,
                                         const shared_ptr<LazyString>& b);

}  // namespace editor
}  // namespace afc

#endif
