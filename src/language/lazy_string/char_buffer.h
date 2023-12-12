#ifndef __AFC_EDITOR_CHAR_BUFFER_H__
#define __AFC_EDITOR_CHAR_BUFFER_H__

#include <memory>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
LazyString NewMoveableCharBuffer(const wchar_t* const* buffer, size_t size);
LazyString NewCharBuffer(const wchar_t* buffer, size_t size);
LazyString NewCharBufferWithOwnership(const wchar_t* buffer, size_t size);
LazyString NewCopyCharBuffer(const wchar_t* buffer);
LazyString NewLazyString(std::wstring input);
LazyString NewLazyString(std::vector<wchar_t> input);
LazyString NewLazyString(ColumnNumberDelta times, wchar_t c);
}  // namespace afc::language::lazy_string

#endif
