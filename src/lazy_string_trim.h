#ifndef __AFC_EDITOR_LAZY_STRING_TRIM_H__
#define __AFC_EDITOR_LAZY_STRING_TRIM_H__

#include <memory>
#include <string>

#include "lazy_string.h"
#include "src/language/safe_types.h"
namespace afc::editor {

// Returns a copy with all left space characters removed.
language::NonNull<std::shared_ptr<LazyString>> StringTrimLeft(
    language::NonNull<std::shared_ptr<LazyString>> a,
    std::wstring space_characters);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LAZY_STRING_TRIM_H__
