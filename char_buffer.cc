#include <cassert>
#include <cstring>
#include <string>

#include "char_buffer.h"

namespace afc {
namespace editor {

using std::string;

class MoveableCharBuffer : public LazyString {
 public:
  MoveableCharBuffer(const char** buffer, size_t size)
      : buffer_(buffer), size_(size) {}

  char get(size_t pos) const {
    assert(pos < size_);
    return (*buffer_)[pos];
  }
  size_t size() const { return size_; }

 protected:
  const char** buffer_;
  size_t size_;
};

class CharBuffer : public MoveableCharBuffer {
 public:
  CharBuffer(const char* buffer, size_t size)
      : MoveableCharBuffer(&location_, size),
        location_(buffer) {}

 protected:
  const char* location_;
};

class CharBufferWithOwnership : public CharBuffer {
 public:
  CharBufferWithOwnership(const char* buffer, size_t size)
      : CharBuffer(buffer, size) {}
  ~CharBufferWithOwnership() { free(const_cast<char*>(location_)); }
};

unique_ptr<LazyString> NewMoveableCharBuffer(const char** buffer, size_t size) {
  return unique_ptr<LazyString>(new MoveableCharBuffer(buffer, size));
}

unique_ptr<LazyString> NewCharBuffer(const char* buffer, size_t size) {
  return unique_ptr<LazyString>(new CharBuffer(buffer, size));
}

unique_ptr<LazyString> NewCharBufferWithOwnership(
    const char* buffer, size_t size) {
  return unique_ptr<LazyString>(new CharBufferWithOwnership(buffer, size));
}

unique_ptr<LazyString> NewCopyCharBuffer(const char* buffer) {
  return unique_ptr<LazyString>(
      new CharBufferWithOwnership(strdup(buffer), strlen(buffer)));
}

unique_ptr<LazyString> NewCopyString(const string& buffer) {
  return std::move(NewCopyCharBuffer(buffer.c_str()));
}

}  // namespace editor
}  // namespace afc
