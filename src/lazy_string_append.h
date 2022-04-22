#ifndef __AFC_EDITOR_LAZY_STRING_APPEND_H__
#define __AFC_EDITOR_LAZY_STRING_APPEND_H__

#include <memory>
#include <vector>

#include "lazy_string.h"
#include "src/language/safe_types.h"
namespace afc::editor {

language::NonNull<std::shared_ptr<LazyString>> StringAppend(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b);
language::NonNull<std::shared_ptr<LazyString>> StringAppend(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b,
    language::NonNull<std::shared_ptr<LazyString>> c);
language::NonNull<std::shared_ptr<LazyString>> StringAppend(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b,
    language::NonNull<std::shared_ptr<LazyString>> c,
    language::NonNull<std::shared_ptr<LazyString>> d);
language::NonNull<std::shared_ptr<LazyString>> Concatenate(
    std::vector<language::NonNull<std::shared_ptr<LazyString>>> inputs);
}  // namespace afc::editor

#endif
