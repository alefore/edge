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

using std::list;
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

class Transformation {
 public:
  virtual ~Transformation() {}
  virtual unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const = 0;
};

class TransformationStack : public Transformation {
 public:
  void PushBack(unique_ptr<Transformation> transformation) {
    stack_.push_back(std::move(transformation));
  }

  void PushFront(unique_ptr<Transformation> transformation) {
    stack_.push_front(std::move(transformation));
  }

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    unique_ptr<TransformationStack> undo(new TransformationStack());
    for (auto& it : stack_) {
      undo->PushFront(it->Apply(editor_state, buffer));
    }
    return std::move(undo);
  }

 private:
  list<unique_ptr<Transformation>> stack_;
};

class OpenBuffer {
 public:
  // A position in a text buffer.
  // TODO: Convert all representations of positions to use this.
  struct Position {
    Position() : line(0), column(0) {}
    Position(size_t l) : line(l), column(0) {}
    Position(size_t l, size_t c) : line(l), column(c) {}

    bool at_beginning_of_line() const { return column == 0; }
    bool at_beginning() const { return line == 0 && at_beginning_of_line(); }

    string ToString() const {
      using std::to_string;
      return to_string(line) + " " + to_string(column);
    }

    size_t line;
    size_t column;
  };

  // Name of a special buffer that shows the list of buffers.
  static const string kBuffersName;

  OpenBuffer(const string& name);

  void Close(EditorState* editor_state);

  void ClearContents();

  virtual void ReloadInto(EditorState* editor_state, OpenBuffer* target) {}
  virtual void Save(EditorState* editor_state);

  void ReadData(EditorState* editor_state);

  void Reload(EditorState* editor_state);
  virtual void EndOfFile(EditorState* editor_state);

  void AppendLazyString(shared_ptr<LazyString> input);
  void AppendLine(shared_ptr<LazyString> line);
  void AppendRawLine(shared_ptr<LazyString> str);

  Position InsertInCurrentPosition(const vector<shared_ptr<Line>>& insertion);
  Position InsertInPosition(
      const vector<shared_ptr<Line>>& insertion, const Position& position);
  // Checks that current_position_col is in the expected range (between 0 and
  // the length of the current line).
  void MaybeAdjustPositionCol();

  void CheckPosition();

  // Sets the positions pointed to by start and end to the beginning and end of
  // the word at the position given by the first argument.  If there's no word
  // in the position given (just a whitespace), moves forward until it finds
  // one.
  //
  // If no word can be found (e.g. we're on a whitespace that's not followed by
  // any word characters), returns false.
  bool BoundWordAt(const Position& position, Position* start, Position* end);

  shared_ptr<Line> current_line() const {
    return LineAt(position_.line);
  }
  shared_ptr<Line> LineAt(size_t line_number) const {
    assert(!contents_.empty());
    assert(line_number <= contents_.size());
    if (line_number == contents_.size()) {
      return nullptr;
    }
    return contents_.at(line_number);
  }
  char character_at(Position position) const {
    return LineAt(position.line)->contents->get(position.column);
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
  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  string ToString() const;

  void replace_current_line(const shared_ptr<Line>& line) {
    contents_.at(position_.line) = line;
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
    return position_.at_beginning();
  }
  bool at_beginning_of_line() const { return at_beginning_of_line(position_); }
  bool at_beginning_of_line(const Position& position) const {
    if (contents_.empty()) { return true; }
    return position.at_beginning_of_line();
  }
  bool at_end() const { return at_end(position_); }
  bool at_end(const Position& position) const {
    if (contents_.empty()) { return true; }
    return at_last_line(position) && at_end_of_line(position);
  }
  Position end_position() const {
    if (contents_.empty()) { return Position(0, 0); }
    return Position(contents_.size() - 1, (*contents_.rbegin())->contents->size());
  }
  bool at_last_line() const { return at_last_line(position_); }
  bool at_last_line(const Position& position) const {
    return position.line == contents_.size() - 1;
  }
  bool at_end_of_line() const {
    return at_end_of_line(position_);
  }
  bool at_end_of_line(const Position& position) const {
    if (contents_.empty()) { return true; }
    return position.column >= LineAt(position.line)->contents->size();
  }
  char current_character() const {
    assert(current_position_col() < current_line()->contents->size());
    return current_line()->contents->get(current_position_col());
  }
  char previous_character() const {
    assert(current_position_col() > 0);
    return current_line()->contents->get(current_position_col() - 1);
  }
  size_t current_position_line() const { return position_.line; }
  void set_current_position_line(size_t value) {
    position_.line = value;
  }
  size_t current_position_col() const { return position_.column; }
  void set_current_position_col(size_t value) {
    position_.column = value;
  }
  const Position position() const {
    return position_;
  }
  void set_position(const Position& position) {
    position_ = position;
    assert(contents_.size() >= 1);
    if (position_.line >= contents_.size()) {
      position_.line = contents_.size() - 1;
    }
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
  static EdgeVariable<char>* variable_save_on_close();
  static EdgeVariable<char>* variable_clear_on_reload();

  static EdgeStruct<string>* StringStruct();
  static EdgeVariable<string>* variable_word_characters();
  static EdgeVariable<string>* variable_path_characters();
  static EdgeVariable<string>* variable_path();

  bool read_bool_variable(const EdgeVariable<char>* variable);
  void set_bool_variable(const EdgeVariable<char>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<char>* variable);

  const string& read_string_variable(const EdgeVariable<string>* variable);
  void set_string_variable(const EdgeVariable<string>* variable,
                           const string& value);

  void Apply(EditorState* editor_state, const Transformation& transformation);
  void Undo(EditorState* editor_state);

 protected:

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
  Position position_;

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

  list<unique_ptr<Transformation>> undo_history_;
};

}  // namespace editor
}  // namespace afc

#endif
