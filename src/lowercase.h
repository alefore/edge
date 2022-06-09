#ifndef __AFC_EDITOR_LOWERCASE_H__
#define __AFC_EDITOR_LOWERCASE_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

// TODO(easy, 2022-06-10): Move this file to src/language/lazy_string.
namespace afc::language::lazy_string {
language::NonNull<std::shared_ptr<LazyString>> LowerCase(
    language::NonNull<std::shared_ptr<LazyString>> input);
}  // namespace afc::language::lazy_string

#endif  // __AFC_EDITOR_LOWERCASE_H__
