#include "substring.h"

#include <cassert>

#include <glog/logging.h>

namespace afc {
namespace editor {

class SubstringImpl : public LazyString {
 public:
  SubstringImpl(const shared_ptr<LazyString>& input, size_t pos, size_t size)
      : buffer_(input), pos_(pos), size_(size) {}

  wchar_t get(size_t pos) const {
    return buffer_->get(pos_ + pos);
  }

  size_t size() const {
    return size_;
  }

 private:
  const shared_ptr<LazyString> buffer_;
  const size_t pos_;
  const size_t size_;
};

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos) {
  return Substring(input, pos, input->size() - pos);
}

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos,
                                 size_t size) {
  assert(input.get());
  if (pos == 0 && size == input->size()) {
    return input;  // Optimization.
  }
  CHECK_LE(pos, input->size());
  CHECK_LE(pos + size, input->size());
  shared_ptr<LazyString> output(new SubstringImpl(input, pos, size));
  return output;
}

}  // namespace editor
}  // namespace afc
