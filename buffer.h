#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <cassert>
#include <map>
#include <memory>
#include <vector>

#include "command_mode.h"
#include "lazy_string.h"
#include "memory_mapped_file.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::map;
using std::max;
using std::min;

struct Line {
  Line() {};
  Line(const shared_ptr<LazyString>& contents_input)
      : contents(contents_input) {}

  size_t size() const { return contents->size(); }

  unique_ptr<EditorMode> activate;
  shared_ptr<LazyString> contents;
};

struct ParseTree {
  string name;
  int length;
  vector<unique_ptr<ParseTree>> items;
};

class OpenBuffer {
 public:
  OpenBuffer();

  virtual void Reload(EditorState* editor_state) {}

  void ReadData(EditorState* editor_state);

  void AppendLazyString(shared_ptr<LazyString> input);
  shared_ptr<Line> AppendLine(shared_ptr<LazyString> line);
  shared_ptr<Line> AppendRawLine(shared_ptr<LazyString> str);

  // Checks that current_position_col is in the expected range (between 0 and
  // the length of the current line).
  void MaybeAdjustPositionCol();

  void CheckPosition();

  shared_ptr<Line> current_line() const {
    assert(!contents_.empty());
    assert(current_position_line_ < contents_.size());
    return contents_.at(current_position_line_);
  }

  int fd() const { return fd_; }

  const vector<shared_ptr<Line>>* contents() const { return &contents_; }
  vector<shared_ptr<Line>>* contents() { return &contents_; }
  bool saveable() const { return saveable_; }

  size_t view_start_line() const { return view_start_line_; }
  void set_view_start_line(size_t value) {
    view_start_line_ = value;
  }
  size_t current_position_line() const { return current_position_line_; }
  void set_current_position_line(size_t value) {
    current_position_line_ = value;
  }
  size_t current_position_col() const { return current_position_col_; }
  void set_current_position_col(size_t value) {
    current_position_col_ = value;
  }

 protected:
  void SetInputFile(int fd);

  vector<unique_ptr<ParseTree>> parse_tree;

  // -1 means "no file descriptor" (i.e. not currently loading this).
  int fd_;
  char* buffer_;
  size_t buffer_line_start_;
  size_t buffer_length_;
  size_t buffer_size_;

  vector<shared_ptr<Line>> contents_;

  size_t view_start_line_;
  size_t current_position_line_;
  size_t current_position_col_;

  bool saveable_;
  bool reading_from_parser_;
};

}  // namespace editor
}  // namespace afc

#endif
