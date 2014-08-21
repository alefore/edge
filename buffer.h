#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <cassert>
#include <map>
#include <memory>
#include <vector>

#include "command_mode.h"
#include "lazy_string.h"
#include "memory_mapped_file.h"
#include "substring.h"
#include "variables.h"

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
  OpenBuffer(const string& name);

  virtual void ReloadInto(EditorState* editor_state, OpenBuffer* target) {}
  virtual void Save(EditorState* editor_state);

  void ReadData(EditorState* editor_state);

  void Reload(EditorState* editor_state);

  void AppendLazyString(shared_ptr<LazyString> input);
  shared_ptr<Line> AppendLine(shared_ptr<LazyString> line);
  shared_ptr<Line> AppendRawLine(shared_ptr<LazyString> str);

  void InsertInCurrentPosition(const vector<shared_ptr<Line>>& insertion);

  // Checks that current_position_col is in the expected range (between 0 and
  // the length of the current line).
  void MaybeAdjustPositionCol();

  void CheckPosition();

  shared_ptr<Line> current_line() const {
    assert(!contents_.empty());
    assert(current_position_line_ <= contents_.size());
    if (current_position_line_ == contents_.size()) {
      return nullptr;
    }
    return contents_.at(current_position_line_);
  }
  // Returns the substring of the current line until the current position.
  shared_ptr<LazyString> current_line_head() const {
    return Substring(current_line()->contents, 0, current_position_col());
  }
  // Returns the substring of the current line from the current to the last
  // position.
  shared_ptr<LazyString> current_line_tail() const {
    return Substring(current_line()->contents, current_position_col());
  }

  void replace_current_line(const shared_ptr<Line>& line) {
    contents_.at(current_position_line_) = line;
  }

  int fd() const { return fd_; }

  const vector<shared_ptr<Line>>* contents() const { return &contents_; }
  vector<shared_ptr<Line>>* contents() { return &contents_; }

  size_t view_start_line() const { return view_start_line_; }
  void set_view_start_line(size_t value) {
    view_start_line_ = value;
  }
  size_t view_start_column() const { return view_start_column_; }
  void set_view_start_column(size_t value) {
    view_start_column_ = value;
  }
  bool at_beginning() const {
    if (contents_.empty()) { return true; }
    return current_position_line_ == 0 && at_beginning_of_line();
  }
  bool at_beginning_of_line() const {
     if (contents_.empty()) { return true; }
    return current_position_col_ == 0;
  }
  bool at_end() const {
    if (contents_.empty()) { return true; }
    return at_last_line() && at_end_of_line();
  }
  bool at_last_line() const {
    return contents_.size() <= current_position_line_ + 1;
  }
  bool at_end_of_line() const {
    if (contents_.empty()) { return true; }
    return current_position_col_ >= current_line()->contents->size();
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

  void Enter(EditorState* editor_state) {
    if (read_bool_variable(variable_reload_on_enter())) {
      Reload(editor_state);
      CheckPosition();
    }
  }

  void set_modified(bool value) { modified_ = value; }
  bool modified() const { return modified_; }

  string FlagsString() const;

  void SetInputFile(int fd, bool fd_is_terminal, pid_t child_pid);

  void CopyVariablesFrom(const shared_ptr<const OpenBuffer>& buffer);

  static EdgeStruct<char>* BoolStruct();
  static EdgeVariable<char>* variable_pts();
  static EdgeVariable<char>* variable_close_after_clean_exit();
  static EdgeVariable<char>* variable_reload_on_enter();
  static EdgeVariable<char>* variable_atomic_lines();
  static EdgeVariable<char>* variable_diff();

  static EdgeStruct<string>* StringStruct();
  static EdgeVariable<string>* variable_word_characters();

  bool read_bool_variable(const EdgeVariable<char>* variable);
  void set_bool_variable(const EdgeVariable<char>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<char>* variable);

  const string& read_string_variable(const EdgeVariable<string>* variable);
  void set_string_variable(const EdgeVariable<string>* variable,
                           const string& value);

 protected:
  void EndOfFile(EditorState* editor_state);

  vector<unique_ptr<ParseTree>> parse_tree;

  string name_;

  // -1 means "no file descriptor" (i.e. not currently loading this).
  int fd_;
  // This is used to remember if we obtained a terminal for the file descriptor
  // (for a subprocess).  Typically this has the same value of pts_ after a
  // subprocess is started, but it's a separate value to allow the user to
  // change the value of pts_ without breaking things (when one command is
  // already running).
  bool fd_is_terminal_;
  char* buffer_;
  size_t buffer_line_start_;
  size_t buffer_length_;
  size_t buffer_size_;
  // -1 means "no child process"
  pid_t child_pid_;
  int child_exit_status_;

  vector<shared_ptr<Line>> contents_;

  size_t view_start_line_;
  size_t view_start_column_;
  size_t current_position_line_;
  size_t current_position_col_;

  bool modified_;
  bool reading_from_parser_;

  // Once we're done reading, should we reload the buffer?  This is used when
  // a reload is triggered while we're reading from an underlying process: we
  // them just set this and kill the underlying process (so that we can avoid
  // blocking the whole process waiting for the process to exit).
  bool reload_after_exit_;

  // This uses char rather than bool because vector<bool>::iterator does not
  // yield a bool& when dereferenced, which makes EdgeStructInstance<bool>
  // incompatible with other template specializations
  // (EdgeStructInstance<bool>::Get would be returning a reference to a
  // temporary variable).
  EdgeStructInstance<char> bool_variables_;
  EdgeStructInstance<string> string_variables_;
};

}  // namespace editor
}  // namespace afc

#endif
