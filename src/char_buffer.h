#ifndef __AFC_EDITOR_CHAR_BUFFER_H__
#define __AFC_EDITOR_CHAR_BUFFER_H__

#include <memory>
#include <vector>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;
using std::vector;

unique_ptr<LazyString> NewMoveableCharBuffer(
    const wchar_t* const* buffer, size_t size);
unique_ptr<LazyString> NewCharBuffer(const wchar_t* buffer, size_t size);
unique_ptr<LazyString> NewCharBufferWithOwnership(
    const wchar_t* buffer, size_t size);
unique_ptr<LazyString> NewCopyCharBuffer(const wchar_t* buffer);
unique_ptr<LazyString> NewCopyString(const wstring& buffer);
unique_ptr<LazyString> NewStringFromVector(vector<wchar_t> input);

}  // namespace editor
}  // namespace afc

#endif
