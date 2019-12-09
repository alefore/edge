#include "src/char_buffer.h"

#include <glog/logging.h>

#include <cstring>
#include <string>

#include "src/line_column.h"

namespace afc {
namespace editor {

namespace {
class RepeatedChar : public LazyString {
 public:
  RepeatedChar(ColumnNumberDelta times, wchar_t c) : times_(times), c_(c) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos.ToDelta(), times_);
    return c_;
  }

  ColumnNumberDelta size() const { return times_; }

 protected:
  const ColumnNumberDelta times_;
  const wchar_t c_;
};

template <typename Container>
class StringFromContainer : public LazyString {
 public:
  StringFromContainer(Container data) : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos, ColumnNumber(data_.size()));
    return data_.at(pos.column);
  }

  ColumnNumberDelta size() const { return ColumnNumberDelta(data_.size()); }

 protected:
  const Container data_;
};

class MoveableCharBuffer : public LazyString {
 public:
  MoveableCharBuffer(const wchar_t* const* buffer, size_t input_size)
      : buffer_(buffer), size_(input_size) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos.ToDelta(), size_);
    return (*buffer_)[pos.column];
  }

  ColumnNumberDelta size() const { return size_; }

 protected:
  const wchar_t* const* buffer_;
  ColumnNumberDelta size_;
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

std::unique_ptr<LazyString> NewMoveableCharBuffer(const wchar_t* const* buffer,
                                                  size_t size) {
  return std::make_unique<MoveableCharBuffer>(buffer, size);
}

std::unique_ptr<LazyString> NewCharBuffer(const wchar_t* buffer, size_t size) {
  return std::make_unique<CharBuffer>(buffer, size);
}

std::unique_ptr<LazyString> NewCharBufferWithOwnership(const wchar_t* buffer,
                                                       size_t size) {
  return std::make_unique<CharBufferWithOwnership>(buffer, size);
}

std::unique_ptr<LazyString> NewCopyCharBuffer(const wchar_t* buffer) {
  return std::make_unique<CharBufferWithOwnership>(wcsdup(buffer),
                                                   wcslen(buffer));
}

std::unique_ptr<LazyString> NewLazyString(std::wstring buffer) {
  return std::make_unique<StringFromContainer<std::wstring>>(std::move(buffer));
}

std::unique_ptr<LazyString> NewLazyString(std::vector<wchar_t> data) {
  return std::make_unique<StringFromContainer<std::vector<wchar_t>>>(
      std::move(data));
}

std::unique_ptr<LazyString> NewLazyString(ColumnNumberDelta times, wchar_t c) {
  return std::make_unique<RepeatedChar>(times, c);
}

}  // namespace editor
}  // namespace afc
