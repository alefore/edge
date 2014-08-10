#include <cassert>
#include <cstring>
#include <string>

#include "char_buffer.h"

namespace afc {
namespace editor {

using std::string;

class CharBuffer : public LazyString {
 public:
  CharBuffer(const char* buffer, size_t size) : buffer_(buffer), size_(size) {}

  char get(size_t pos) {
    assert(pos < size_);
    return buffer_[pos];
  }
  size_t size() { return size_; }

 protected:
  const char* buffer_;
  int size_;
};

unique_ptr<LazyString> NewCharBuffer(const char* buffer, size_t size) {
  return unique_ptr<LazyString>(new CharBuffer(buffer, size));
}

class CopyCharBuffer : public CharBuffer {
 public:
  CopyCharBuffer(const char* buffer)
      : CharBuffer(strdup(buffer), strlen(buffer)) {}
  ~CopyCharBuffer() { free(const_cast<char*>(buffer_)); }
};

unique_ptr<LazyString> NewCopyCharBuffer(const char* buffer) {
  return unique_ptr<LazyString>(new CopyCharBuffer(buffer));
}

unique_ptr<LazyString> NewCopyString(const string& buffer) {
  return std::move(NewCopyCharBuffer(buffer.c_str()));
}

}  // namespace editor
}  // namespace afc
