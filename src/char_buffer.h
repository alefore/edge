#ifndef __AFC_EDITOR_CHAR_BUFFER_H__
#define __AFC_EDITOR_CHAR_BUFFER_H__

#include <memory>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::editor {
class ColumnNumberDelta;

language::NonNull<std::unique_ptr<LazyString>> NewMoveableCharBuffer(
    const wchar_t* const* buffer, size_t size);
language::NonNull<std::unique_ptr<LazyString>> NewCharBuffer(
    const wchar_t* buffer, size_t size);
language::NonNull<std::unique_ptr<LazyString>> NewCharBufferWithOwnership(
    const wchar_t* buffer, size_t size);
language::NonNull<std::unique_ptr<LazyString>> NewCopyCharBuffer(
    const wchar_t* buffer);
language::NonNull<std::unique_ptr<LazyString>> NewLazyString(
    std::wstring input);
language::NonNull<std::unique_ptr<LazyString>> NewLazyString(
    std::vector<wchar_t> input);

language::NonNull<std::unique_ptr<LazyString>> NewLazyString(
    ColumnNumberDelta times, wchar_t c);

}  // namespace afc::editor

#endif
