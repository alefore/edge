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

  virtual void ReloadInto(EditorState* editor_state, OpenBuffer* target) {}
  virtual void Save(EditorState* editor_state);

  void Reload(EditorState* editor_state) {
    ReloadInto(editor_state, this);
  }
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

  size_t view_start_line() const { return view_start_line_; }
  void set_view_start_line(size_t value) {
    view_start_line_ = value;
  }
  bool at_beginning() const {
    if (contents_.empty()) { return true; }
    return current_position_line_ == 0 && current_position_col_ == 0;
  }
  bool at_end() const {
    if (contents_.empty()) { return true; }
    return current_position_line_ + 1 >= contents_.size()
        && current_position_col_ >= current_line()->contents->size();
  }
  char current_character() const {
    assert(current_position_col() < current_line()->contents->size());
    return current_line()->contents->get(current_position_col());
  }
  char previous_character() const {
    assert(current_position_col() > 0);
    return current_line()->contents->get(current_position_col() - 1);
  }
  size_t current_position_line() const { return current_position_line_; }
  void set_current_position_line(size_t value) {
    current_position_line_ = value;
  }
  size_t current_position_col() const { return current_position_col_; }
  void set_current_position_col(size_t value) {
    current_position_col_ = value;
  }

  void toggle_reload_on_enter() {
    reload_on_enter_ = !reload_on_enter_;
  }
  bool reload_on_enter() const { return reload_on_enter_; }
  void set_reload_on_enter(bool value) {
    reload_on_enter_ = value;
  }
  void Enter(EditorState* editor_state) {
    if (reload_on_enter_) {
      Reload(editor_state);
      CheckPosition();
    }
  }

  void set_modified(bool value) { modified_ = value; }
  bool modified() const { return modified_; }

  string FlagsString() const;

  void SetInputFile(int fd);

  void toggle_diff() { diff_ = !diff_; }

  bool* word_characters() { return word_characters_; }

 protected:
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

  bool modified_;
  bool reading_from_parser_;

  // Variables that can be set from the editor.
  bool reload_on_enter_;
  // Does this buffer represent a diff?  Changes the way 'Save' behaves.
  bool diff_;

  bool word_characters_[256];
};

}  // namespace editor
}  // namespace afc

#endif
