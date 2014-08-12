#include "buffer.h"

#include <cassert>
#include <memory>
#include <string>

#include "char_buffer.h"
#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

OpenBuffer::OpenBuffer()
    : fd_(-1),
      buffer_(nullptr),
      buffer_line_start_(0),
      buffer_length_(0),
      buffer_size_(0),
      view_start_line_(0),
      current_position_line_(0),
      current_position_col_(0),
      saveable_(false) {}

void OpenBuffer::ReadData(EditorState* editor_state) {
  assert(fd_ > 0);
  assert(buffer_line_start_ <= buffer_length_);
  assert(buffer_length_ <= buffer_size_);
  if (buffer_length_ == buffer_size_) {
    buffer_size_ = buffer_size_ ? buffer_size_ * 2 : 64 * 1024;
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_size_));
  }
  int characters_read = read(fd_, buffer_ + buffer_length_, buffer_size_ - buffer_length_);
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    // TODO: Handle this better.
    exit(1);
  }
  if (characters_read == 0) {
    close(fd_);
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_length_));
    fd_ = -1;
  }

  shared_ptr<LazyString> buffer_wrapper(
      NewMoveableCharBuffer(&buffer_, buffer_length_ + characters_read));
  for (size_t i = buffer_length_;
       i < buffer_length_ + static_cast<size_t>(characters_read);
       i++) {
    if (buffer_[i] == '\n') {
      AppendLine(Substring(buffer_wrapper, buffer_line_start_, i - buffer_line_start_ - 1));
      buffer_line_start_ = i + 1;
      editor_state->screen_needs_redraw = true;
    }
  }
  buffer_length_ += characters_read;
}

void OpenBuffer::AppendLazyString(shared_ptr<LazyString> input) {
  size_t size = input->size();
  size_t start = 0;
  for (size_t i = 0; i < size; i++) {
    if (input->get(i) == '\n') {
      AppendLine(Substring(input, start, i - start));
      start = i + 1;
    }
  }
}

shared_ptr<Line> OpenBuffer::AppendLine(shared_ptr<LazyString> str) {
  shared_ptr<Line> line(new Line);
  line->contents = str;
  contents_.push_back(line);
  return line;
}

void OpenBuffer::MaybeAdjustPositionCol() {
  size_t line_length = current_line()->contents->size();
  if (current_position_col_ > line_length) {
    current_position_col_ = line_length;
  }
}

void OpenBuffer::CheckPosition() {
  if (current_position_line_ >= contents_.size()) {
    current_position_line_ = contents_.size();
    if (current_position_line_ > 0) {
      current_position_line_--;
    }
  }
}

}  // namespace editor
}  // namespace afc
