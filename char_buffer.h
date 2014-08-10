#ifndef __AFC_EDITOR_CHAR_BUFFER_H__
#define __AFC_EDITOR_CHAR_BUFFER_H__

#include <memory>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;

unique_ptr<LazyString> NewCharBuffer(const char* buffer, size_t size);
unique_ptr<LazyString> NewCopyCharBuffer(const char* buffer);
unique_ptr<LazyString> NewCopyString(const string& buffer);

}  // namespace editor
}  // namespace afc

#endif
