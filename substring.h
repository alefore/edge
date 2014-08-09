#ifndef __AFC_EDITOR_SUBSTRING_H__
#define __AFC_EDITOR_SUBSTRING_H__

#include <memory>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;

shared_ptr<LazyString> Substring(
    const shared_ptr<LazyString>& input, size_t pos, size_t size);

}  // namespace editor
}  // namespace afc

#endif
