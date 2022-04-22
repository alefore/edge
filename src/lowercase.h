#ifndef __AFC_EDITOR_LOWERCASE_H__
#define __AFC_EDITOR_LOWERCASE_H__

#include <memory>

#include "lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::editor {

using std::shared_ptr;

language::NonNull<shared_ptr<LazyString>> LowerCase(
    language::NonNull<shared_ptr<LazyString>> input);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LOWERCASE_H__
