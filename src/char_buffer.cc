#include <cstring>
#include <string>

#include <glog/logging.h>

#include "char_buffer.h"

namespace afc {
namespace editor {

using std::string;

class StringFromVector : public LazyString {
 public:
  StringFromVector(vector<wchar_t> data) : data_(std::move(data)) {}

  wchar_t get(size_t pos) const {
    CHECK_LT(pos, data_.size());
    return data_[pos];
  }
  size_t size() const { return data_.size(); }

 protected:
  const vector<wchar_t> data_;
};

class MoveableCharBuffer : public LazyString {
 public:
  MoveableCharBuffer(const wchar_t* const* buffer, size_t input_size)
      : buffer_(buffer), size_(input_size) {}

  wchar_t get(size_t pos) const {
    CHECK_LT(pos, size_);
    return (*buffer_)[pos];
  }
  size_t size() const { return size_; }

 protected:
  const wchar_t* const* buffer_;
  size_t size_;
};

class CharBuffer : public MoveableCharBuffer {
 public:
  CharBuffer(const wchar_t* buffer, size_t input_size)
      : MoveableCharBuffer(&location_, input_size), location_(buffer) {}

 protected:
  const wchar_t* location_;
};

class CharBufferWithOwnership : public CharBuffer {
 public:
  CharBufferWithOwnership(const wchar_t* buffer, size_t input_size)
      : CharBuffer(buffer, input_size) {}
  ~CharBufferWithOwnership() { free(const_cast<wchar_t*>(location_)); }
};

unique_ptr<LazyString> NewMoveableCharBuffer(const wchar_t* const* buffer,
                                             size_t size) {
  return std::make_unique<MoveableCharBuffer>(buffer, size);
}

unique_ptr<LazyString> NewCharBuffer(const wchar_t* buffer, size_t size) {
  return std::make_unique<CharBuffer>(buffer, size);
}

unique_ptr<LazyString> NewCharBufferWithOwnership(const wchar_t* buffer,
                                                  size_t size) {
  return std::make_unique<CharBufferWithOwnership>(buffer, size);
}

unique_ptr<LazyString> NewCopyCharBuffer(const wchar_t* buffer) {
  return std::make_unique<CharBufferWithOwnership>(wcsdup(buffer),
                                                   wcslen(buffer));
}

unique_ptr<LazyString> NewCopyString(const wstring& buffer) {
  return std::move(NewCopyCharBuffer(buffer.c_str()));
}

unique_ptr<LazyString> NewStringFromVector(vector<wchar_t> data) {
  return std::make_unique<StringFromVector>(std::move(data));
}

}  // namespace editor
}  // namespace afc
