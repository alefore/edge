#include "src/language/lazy_string/char_buffer.h"

#include <glog/logging.h>

#include <cstring>
#include <string>

#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
namespace {
class RepeatedChar : public LazyStringImpl {
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
class StringFromContainer : public LazyStringImpl {
 public:
  StringFromContainer(Container data) : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos, ColumnNumber(data_.size()));
    return data_.at(pos.read());
  }

  ColumnNumberDelta size() const { return ColumnNumberDelta(data_.size()); }

 protected:
  const Container data_;
};

class MoveableCharBuffer : public LazyStringImpl {
 public:
  MoveableCharBuffer(const wchar_t* const* buffer, size_t input_size)
      : buffer_(buffer), size_(input_size) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos.ToDelta(), size_);
    return (*buffer_)[pos.read()];
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

LazyString NewMoveableCharBuffer(const wchar_t* const* buffer, size_t size) {
  return LazyString(MakeNonNullShared<MoveableCharBuffer>(buffer, size));
}

LazyString NewCharBuffer(const wchar_t* buffer, size_t size) {
  return LazyString(MakeNonNullShared<CharBuffer>(buffer, size));
}

LazyString NewCharBufferWithOwnership(const wchar_t* buffer, size_t size) {
  return LazyString(MakeNonNullShared<CharBufferWithOwnership>(buffer, size));
}

LazyString NewCopyCharBuffer(const wchar_t* buffer) {
  return LazyString(MakeNonNullShared<CharBufferWithOwnership>(wcsdup(buffer),
                                                               wcslen(buffer)));
}

LazyString NewLazyString(std::vector<wchar_t> data) {
  return LazyString(
      MakeNonNullShared<StringFromContainer<std::vector<wchar_t>>>(
          std::move(data)));
}

LazyString NewLazyString(ColumnNumberDelta times, wchar_t c) {
  return LazyString(MakeNonNullShared<RepeatedChar>(times, c));
}

}  // namespace afc::language::lazy_string
