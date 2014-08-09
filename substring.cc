#include "substring.h"

namespace afc {
namespace editor {

class SubstringImpl : public LazyString {
 public:
  SubstringImpl(const shared_ptr<LazyString>& input, size_t pos, size_t size)
      : buffer_(input), pos_(pos), size_(size) {}

  char get(size_t pos) {
    return buffer_->get(pos_ + pos);
  }

  size_t size() {
    return size_;
  }

 private:
  const shared_ptr<LazyString> buffer_;
  const size_t pos_;
  const size_t size_;
};

shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos,
                                 size_t size) {
  shared_ptr<LazyString> output(new SubstringImpl(input, pos, size));
  return output;
}

}  // namespace editor
}  // namespace afc
