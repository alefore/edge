#include "src/char_buffer.h"

#include <glog/logging.h>

#include <cstring>
#include <string>

#include "src/language/safe_types.h"
#include "src/line_column.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
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
    return data_.at(pos.read());
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

NonNull<std::unique_ptr<LazyString>> NewMoveableCharBuffer(
    const wchar_t* const* buffer, size_t size) {
  return MakeNonNullUnique<MoveableCharBuffer>(buffer, size);
}

NonNull<std::unique_ptr<LazyString>> NewCharBuffer(const wchar_t* buffer,
                                                   size_t size) {
  return MakeNonNullUnique<CharBuffer>(buffer, size);
}

NonNull<std::unique_ptr<LazyString>> NewCharBufferWithOwnership(
    const wchar_t* buffer, size_t size) {
  return MakeNonNullUnique<CharBufferWithOwnership>(buffer, size);
}

NonNull<std::unique_ptr<LazyString>> NewCopyCharBuffer(const wchar_t* buffer) {
  return MakeNonNullUnique<CharBufferWithOwnership>(wcsdup(buffer),
                                                    wcslen(buffer));
}

NonNull<std::unique_ptr<LazyString>> NewLazyString(std::wstring buffer) {
  return MakeNonNullUnique<StringFromContainer<std::wstring>>(
      std::move(buffer));
}

NonNull<std::unique_ptr<LazyString>> NewLazyString(std::vector<wchar_t> data) {
  return MakeNonNullUnique<StringFromContainer<std::vector<wchar_t>>>(
      std::move(data));
}

NonNull<std::unique_ptr<LazyString>> NewLazyString(ColumnNumberDelta times,
                                                   wchar_t c) {
  return MakeNonNullUnique<RepeatedChar>(times, c);
}

}  // namespace afc::editor
