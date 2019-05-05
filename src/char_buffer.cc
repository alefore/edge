#include "src/char_buffer.h"

#include <glog/logging.h>

#include <cstring>
#include <string>

#include "src/line_column.h"

namespace afc {
namespace editor {

using std::string;

namespace {
class RepeatedChar : public LazyString {
 public:
  RepeatedChar(ColumnNumberDelta times, wchar_t c) : times_(times), c_(c) {}

  wchar_t get(size_t pos) const {
    CHECK_LT(ColumnNumberDelta(pos), times_);
    return c_;
  }
  size_t size() const { return times_.column_delta; }

 protected:
  const ColumnNumberDelta times_;
  const wchar_t c_;
};

template <typename Container>
class StringFromContainer : public LazyString {
 public:
  StringFromContainer(Container data) : data_(std::move(data)) {}

  wchar_t get(size_t pos) const {
    CHECK_LT(pos, data_.size());
    return data_.at(pos);
  }
  size_t size() const { return data_.size(); }

 protected:
  const Container data_;
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
}  // namespace

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

unique_ptr<LazyString> NewLazyString(wstring buffer) {
  return std::make_unique<StringFromContainer<wstring>>(std::move(buffer));
}

unique_ptr<LazyString> NewLazyString(vector<wchar_t> data) {
  return std::make_unique<StringFromContainer<vector<wchar_t>>>(
      std::move(data));
}

std::unique_ptr<LazyString> NewLazyString(ColumnNumberDelta times, wchar_t c) {
  return std::make_unique<RepeatedChar>(times, c);
}

}  // namespace editor
}  // namespace afc
