#include "buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "run_command_handler.h"
#include "lazy_string_append.h"
#include "substring.h"

namespace {

using namespace afc::editor;
using std::cerr;

void SaveDiff(EditorState* editor_state, OpenBuffer* buffer) {
  unique_ptr<OpenBuffer> original(new OpenBuffer("- original diff"));
  buffer->ReloadInto(editor_state, original.get());
  while (original->fd() != -1) {
    original->ReadData(editor_state);
  }

  char* path_old_diff = strdup("patch-old-diff-XXXXXX");
  int fd_old_diff = mkstemp(path_old_diff);
  char* path_new_diff = strdup("patch-new-diff-XXXXXX");
  int fd_new_diff = mkstemp(path_new_diff);

  SaveContentsToOpenFile(editor_state, original.get(), path_old_diff, fd_old_diff);
  SaveContentsToOpenFile(editor_state, buffer, path_new_diff, fd_new_diff);
  close(fd_old_diff);
  close(fd_new_diff);
  RunCommandHandler("./diff_writer.py " + string(path_old_diff) + " " + string(path_new_diff), editor_state);
  editor_state->SetStatus("Changing diff");
}

}  // namespace

namespace afc {
namespace editor {

using std::to_string;

OpenBuffer::OpenBuffer(const string& name)
    : name_(name),
      fd_(-1),
      fd_is_terminal_(false),
      buffer_(nullptr),
      buffer_line_start_(0),
      buffer_length_(0),
      buffer_size_(0),
      child_pid_(-1),
      child_exit_status_(0),
      view_start_line_(0),
      current_position_line_(0),
      current_position_col_(0),
      modified_(false),
      reading_from_parser_(false),
      reload_after_exit_(false),
      close_after_clean_exit_(false),
      reload_on_enter_(false),
      diff_(false),
      atomic_lines_(false),
      pts_(false) {
  set_word_characters(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
}

void OpenBuffer::EndOfFile(EditorState* editor_state) {
  close(fd_);
  buffer_ = static_cast<char*>(realloc(buffer_, buffer_length_));
  if (child_pid_ != -1) {
    if (waitpid(child_pid_, &child_exit_status_, 0) == -1) {
      editor_state->SetStatus("waitpid failed: " + string(strerror(errno)));
      return;
    }
  }
  fd_ = -1;
  child_pid_ = -1;
  if (reload_after_exit_) {
    reload_after_exit_ = false;
    Reload(editor_state);
  }
  if (close_after_clean_exit_
      && WIFEXITED(child_exit_status_)
      && WEXITSTATUS(child_exit_status_) == 0) {
    auto it = editor_state->buffers()->find(name_);
    if (it != editor_state->buffers()->end()) {
      editor_state->CloseBuffer(it);
    }
  }
}

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
    return EndOfFile(editor_state);
  }
  assert(characters_read >= 0);
  if (characters_read == 0) {
    return EndOfFile(editor_state);
  }

  shared_ptr<LazyString> buffer_wrapper(
      NewMoveableCharBuffer(
          &buffer_, buffer_length_ + static_cast<size_t>(characters_read)));
  for (size_t i = buffer_length_;
       i < buffer_length_ + static_cast<size_t>(characters_read);
       i++) {
    if (buffer_[i] == '\n') {
      if (current_position_line_ == contents_.size()) {
        current_position_line_ ++;
      }
      AppendLine(Substring(buffer_wrapper, buffer_line_start_, i - buffer_line_start_));
      assert(current_position_line_ <= contents_.size());
      buffer_line_start_ = i + 1;
      if (editor_state->has_current_buffer()
          && editor_state->current_buffer()->second.get() == this
          && contents_.size() <= view_start_line_ + editor_state->visible_lines()) {
        editor_state->ScheduleRedraw();
      }
    }
  }
  buffer_length_ += static_cast<size_t>(characters_read);
}

void OpenBuffer::Reload(EditorState* editor_state) {
  if (child_pid_ != -1) {
    kill(-child_pid_, SIGTERM);
    reload_after_exit_ = true;
    return;
  }
  ReloadInto(editor_state, this);
  set_modified(false);
  CheckPosition();
}

void OpenBuffer::Save(EditorState* editor_state) {
  if (diff_) {
    SaveDiff(editor_state, this);
    return;
  }
  editor_state->SetStatus("Buffer can't be saved.");
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

void OpenBuffer::InsertInCurrentPosition(const vector<shared_ptr<Line>>& insertion) {
  auto begin = insertion.begin();
  auto end = insertion.end();
  if (begin == end) { return; }
  CheckPosition();
  MaybeAdjustPositionCol();
  if (!at_beginning_of_line()) {
    auto tail = current_line_tail();
    replace_current_line(shared_ptr<Line>(new Line(
        StringAppend(current_line_head(), (*begin)->contents))));
    begin++;
    current_position_col_ = 0;
    current_position_line_ ++;
    contents_.insert(contents_.begin() + current_position_line_,
        shared_ptr<Line>(new Line(tail)));
  }
  if (begin == end) { return; }
  contents_.insert(contents_.begin() + current_position_line_, begin, end);
  current_position_line_ += end - begin;
  current_position_col_ = 0;
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

void OpenBuffer::SetInputFile(
    int input_fd, bool fd_is_terminal, pid_t child_pid) {
  contents_.clear();
  buffer_ = nullptr;
  buffer_line_start_ = 0;
  buffer_length_ = 0;
  buffer_size_ = 0;
  if (fd_ != -1) {
    close(fd_);
  }
  assert(child_pid_ == -1);
  fd_ = input_fd;
  fd_is_terminal_ = fd_is_terminal;
  child_pid_ = child_pid;
}

string OpenBuffer::FlagsString() const {
  string output;
  if (modified()) {
    output += "~";
  }
  if (fd() != -1) {
    output += "< l:" + to_string(contents_.size());
  }
  if (child_pid_ != -1) {
    output += " pid:" + to_string(child_pid_);
  } else if (child_exit_status_ != 0) {
    if (WIFEXITED(child_exit_status_)) {
      output += " exit:" + to_string(WEXITSTATUS(child_exit_status_));
    } else if (WIFSIGNALED(child_exit_status_)) {
      output += " signal:" + to_string(WTERMSIG(child_exit_status_));
    } else {
      output += " exit-status:" + to_string(child_exit_status_);
    }
  }
  return output;
}

void OpenBuffer::set_word_characters(const string& word_characters) {
  for (size_t i = 0; i < sizeof(word_characters_); i++) {
    word_characters_[i] = word_characters.find(static_cast<char>(i))
        != word_characters.npos;
  }
}

void OpenBuffer::CopyVariablesFrom(const shared_ptr<OpenBuffer>& src) {
  assert(src.get() != nullptr);
  reload_after_exit_ = src->reload_after_exit_;
  close_after_clean_exit_ = src->close_after_clean_exit_;
  reload_on_enter_ = src->reload_on_enter_;
  diff_ = src->diff_;
  atomic_lines_ = src->atomic_lines_;
  for (size_t i = 0; i < sizeof(word_characters_); i++) {
    word_characters_[i] = src->word_characters_[i];
  }
}

}  // namespace editor
}  // namespace afc
