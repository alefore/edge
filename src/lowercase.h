#ifndef __AFC_EDITOR_LOWERCASE_H__
#define __AFC_EDITOR_LOWERCASE_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::editor {
language::NonNull<std::shared_ptr<LazyString>> LowerCase(
    language::NonNull<std::shared_ptr<LazyString>> input);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LOWERCASE_H__
