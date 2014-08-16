#include "buffer.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

using std::to_string;

OpenBuffer::OpenBuffer()
    : fd_(-1),
      buffer_(nullptr),
      buffer_line_start_(0),
      buffer_length_(0),
      buffer_size_(0),
      view_start_line_(0),
      current_position_line_(0),
      current_position_col_(0),
      modified_(false),
      saveable_(false),
      reading_from_parser_(false),
      reload_on_enter_(false) {}

void OpenBuffer::ReadData(EditorState* editor_state) {
  assert(fd_ > 0);
  assert(buffer_line_start_ <= buffer_length_);
  assert(buffer_length_ <= buffer_size_);
  if (buffer_length_ == buffer_size_) {
    buffer_size_ = buffer_size_ ? buffer_size_ * 2 : 64 * 1024;
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_size_));
  }
  ssize_t characters_read = read(fd_, buffer_ + buffer_length_, buffer_size_ - buffer_length_);
  if (characters_read == -1) {
    if (errno == EAGAIN) {
      return;
    }
    // TODO: Handle this better.
    exit(1);
  }
  assert(characters_read >= 0);
  if (characters_read == 0) {
    close(fd_);
    buffer_ = static_cast<char*>(realloc(buffer_, buffer_length_));
    fd_ = -1;
  }

  shared_ptr<LazyString> buffer_wrapper(
      NewMoveableCharBuffer(
          &buffer_, buffer_length_ + static_cast<size_t>(characters_read)));
  for (size_t i = buffer_length_;
       i < buffer_length_ + static_cast<size_t>(characters_read);
       i++) {
    if (buffer_[i] == '\n') {
      AppendLine(Substring(buffer_wrapper, buffer_line_start_, i - buffer_line_start_));
      buffer_line_start_ = i + 1;
      if (editor_state->current_buffer != editor_state->buffers.end()
          && editor_state->get_current_buffer().get() == this) {
        editor_state->screen_needs_redraw = true;
      }
    }
  }
  buffer_length_ += static_cast<size_t>(characters_read);
}

void OpenBuffer::Save(EditorState* editor_state) {
  if (!saveable()) {
    editor_state->status = "Buffer can't be saved.";
    return;
  }
  string path = editor_state->current_buffer->first;
  string tmp_path = path + ".tmp";
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd == -1) {
    editor_state->status = tmp_path + ": open failed: " + strerror(errno);
    return;
  }
  for (const auto& line : contents_) {
    const auto& str = *line->contents;
    char* tmp = static_cast<char*>(malloc(str.size() + 1));
    strcpy(tmp, str.ToString().c_str());
    tmp[str.size()] = '\n';
    int write_result = write(fd, tmp, str.size() + 1);
    free(tmp);
    if (write_result == -1) {
      editor_state->status = tmp_path + ": write failed: " + to_string(fd)
          + ": " + strerror(errno);
      close(fd);
      return;
    }
  }
  close(fd);
  if (rename(tmp_path.c_str(), path.c_str()) == -1) {
    editor_state->status = path + ": rename failed: " + strerror(errno);
    return;
  }
  set_modified(false);
  editor_state->status = "Saved: " + path;
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

static void AddToParseTree(const shared_ptr<LazyString>& str_input) {
  string str = str_input->ToString();
}

shared_ptr<Line> OpenBuffer::AppendLine(shared_ptr<LazyString> str) {
  if (reading_from_parser_) {
    switch (str->get(0)) {
      case 'E':
        return AppendRawLine(Substring(str, 1));

      case 'T':
        AddToParseTree(str);
        return nullptr;
    }
    return nullptr;
  }

  if (contents_.empty()) {
    if (str->ToString() == "EDGE PARSER v1.0") {
      reading_from_parser_ = true;
      return nullptr;
    }
  }

  return AppendRawLine(str);
}

shared_ptr<Line> OpenBuffer::AppendRawLine(shared_ptr<LazyString> str) {
  shared_ptr<Line> line(new Line);
  line->contents = str;
  contents_.push_back(line);
  return line;
}

void OpenBuffer::MaybeAdjustPositionCol() {
  if (contents_.empty()) { return; }
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

void OpenBuffer::SetInputFile(int input_fd) {
  contents_.clear();
  buffer_ = nullptr;
  buffer_line_start_ = 0;
  buffer_length_ = 0;
  buffer_size_ = 0;
  if (fd_ != -1) {
    close(fd_);
  }
  fd_ = input_fd;
}

}  // namespace editor
}  // namespace afc
