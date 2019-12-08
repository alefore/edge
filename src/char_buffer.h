#ifndef __AFC_EDITOR_CHAR_BUFFER_H__
#define __AFC_EDITOR_CHAR_BUFFER_H__

#include <memory>
#include <vector>

#include "src/lazy_string.h"

namespace afc {
namespace editor {

class ColumnNumberDelta;

std::unique_ptr<LazyString> NewMoveableCharBuffer(const wchar_t* const* buffer,
                                                  size_t size);
std::unique_ptr<LazyString> NewCharBuffer(const wchar_t* buffer, size_t size);
std::unique_ptr<LazyString> NewCharBufferWithOwnership(const wchar_t* buffer,
                                                       size_t size);
std::unique_ptr<LazyString> NewCopyCharBuffer(const wchar_t* buffer);
std::unique_ptr<LazyString> NewLazyString(std::wstring input);
std::unique_ptr<LazyString> NewLazyString(std::vector<wchar_t> input);

std::unique_ptr<LazyString> NewLazyString(ColumnNumberDelta times, wchar_t c);

}  // namespace editor
}  // namespace afc

#endif
