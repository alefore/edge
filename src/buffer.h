#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <cassert>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "command_mode.h"
#include "lazy_string.h"
#include "line.h"
#include "line_column.h"
#include "memory_mapped_file.h"
#include "substring.h"
#include "transformation.h"
#include "variables.h"
#include "vm/public/environment.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

using std::iterator;
using std::list;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::map;
using std::max;
using std::min;
using std::multimap;
using std::ostream;

using namespace afc::vm;

struct ParseTree {
  string name;
  int length;
  vector<unique_ptr<ParseTree>> items;
};

template <typename T, typename B>
class BufferLineGenericIterator
    : public std::iterator<std::input_iterator_tag, shared_ptr<Line>> {
 public:
  BufferLineGenericIterator(B buffer, size_t line)
      : buffer_(buffer), line_(line) {}

  BufferLineGenericIterator(const BufferLineGenericIterator& other)
      : buffer_(other.buffer_),
        line_(other.line_) {}

  BufferLineGenericIterator& operator++();

  BufferLineGenericIterator operator++(int) {
    BufferLineGenericIterator copy(*this);
    operator++();
    return copy;
  }

  BufferLineGenericIterator& operator--();

  BufferLineGenericIterator operator--(int) {
    BufferLineGenericIterator copy(*this);
    operator--();
    return copy;
  }

  bool operator==(const BufferLineGenericIterator& rhs) const {
    return buffer_ == rhs.buffer_ && line_ == rhs.line_;
  }

  bool operator!=(const BufferLineGenericIterator& rhs) const {
    return !(*this == rhs);
  }

  T& operator*();
  const T& operator*() const;

  size_t line() const { return line_; }
  B buffer() { return buffer_; }
  const B buffer() const { return buffer_; }

 private:
  B buffer_;
  size_t line_;
};

template <typename T, typename B>
BufferLineGenericIterator<T, B>& BufferLineGenericIterator<T, B>::operator++() {
  while (line_ < buffer_->contents()->size()) {
    ++line_;
    if (buffer_->IsLineFiltered(line_)) {
      return *this;
    }
  }
  return *this;
}

template <typename T, typename B>
BufferLineGenericIterator<T, B>& BufferLineGenericIterator<T, B>::operator--() {
  while (line_ > 0) {
    --line_;
    if (buffer_->IsLineFiltered(line_)) {
      return *this;
    }
  }
  return *this;
}

template <typename T, typename B>
T& BufferLineGenericIterator<T, B>::operator*() {
  return const_cast<T&>(
      const_cast<const BufferLineGenericIterator<T, B>*>(this)->operator*());
}

template <typename T, typename B>
const T& BufferLineGenericIterator<T, B>::operator*() const {
  return buffer_->contents()->at(line_);
}

typedef BufferLineGenericIterator<shared_ptr<Line>, OpenBuffer*>
        BufferLineIterator;

class BufferLineConstIterator
    : public BufferLineGenericIterator<const shared_ptr<Line>,
                                       const OpenBuffer*> {
 public:
  BufferLineConstIterator(const OpenBuffer* buffer, size_t line)
      : BufferLineGenericIterator(buffer, line) {}

  BufferLineConstIterator(const BufferLineConstIterator& other)
      : BufferLineGenericIterator(other) {}

  BufferLineConstIterator(const BufferLineIterator& input)
      : BufferLineGenericIterator(input.buffer(), input.line()) {}
};

class BufferLineReverseIterator
    : public std::reverse_iterator<BufferLineIterator> {
 public:
  BufferLineReverseIterator(const BufferLineIterator& input_base)
      : std::reverse_iterator<BufferLineIterator>(input_base) {}

  size_t line() const {
    BufferLineIterator tmp(base());
    --tmp;
    return tmp.line();
  }
};

class OpenBuffer {
 public:
  // Name of a special buffer that shows the list of buffers.
  static const string kBuffersName;
  static const string kPasteBuffer;

  static void RegisterBufferType(EditorState* editor_state,
                                 Environment* environment);

  OpenBuffer(EditorState* editor_state, const string& name);
  ~OpenBuffer();

  void Close(EditorState* editor_state);

  void ClearContents();

  virtual void ReloadInto(EditorState*, OpenBuffer*) {}
  virtual void Save(EditorState* editor_state);

  void ReadData(EditorState* editor_state);

  void Reload(EditorState* editor_state);
  virtual void EndOfFile(EditorState* editor_state);

  void AppendLazyString(EditorState* editor_state, shared_ptr<LazyString> input);
  void AppendLine(EditorState* editor_state, shared_ptr<LazyString> line);
  virtual void AppendRawLine(EditorState* editor_state, shared_ptr<LazyString> str);
  size_t ProcessTerminalEscapeSequence(
      EditorState* editor_state, shared_ptr<LazyString> str, size_t read_index,
      std::unordered_set<Line::Modifier, hash<int>>* modifiers);
  void AppendToLastLine(EditorState* editor_state, shared_ptr<LazyString> str);
  void AppendToLastLine(
      EditorState* editor_state, shared_ptr<LazyString> str,
      const vector<unordered_set<Line::Modifier, hash<int>>>& modifiers);

  void EvaluateString(EditorState* editor_state, const string& str);
  void EvaluateFile(EditorState* editor_state, const string& path);

  const string& name() const { return name_; }

  LineColumn InsertInCurrentPosition(const vector<shared_ptr<Line>>& insertion);
  LineColumn InsertInPosition(
      const vector<shared_ptr<Line>>& insertion, const LineColumn& position);
  // Checks that current_position_col is in the expected range (between 0 and
  // the length of the current line).
  void MaybeAdjustPositionCol();

  // Makes sure that the current line (position) is not greater than the number
  // of elements in contents().  Note that after this, it may still not be a
  // valid index for contents() (it may be just at the end).
  void CheckPosition();

  // Sets the positions pointed to by start and end to the beginning and end of
  // the word at the position given by the first argument.  If there's no word
  // in the position given (just a whitespace), moves forward until it finds
  // one.
  //
  // If no word can be found (e.g. we're on a whitespace that's not followed by
  // any word characters), returns false.
  bool BoundWordAt(
      const LineColumn& position, LineColumn* start, LineColumn* end);

  const shared_ptr<Line> current_line() const {
    if (end() == BufferLineConstIterator(line_)) { return nullptr; }
    return *line_;
  }
  shared_ptr<Line> current_line() {
    if (end() == line_) { return nullptr; }
    return *line_;
  }
  shared_ptr<Line> LineAt(size_t line_number) const {
    CHECK(!contents_.empty());
    CHECK_LE(line_number, contents_.size());
    if (line_number == contents_.size()) {
      return nullptr;
    }
    return contents_.at(line_number);
  }
  char character_at(LineColumn position) const {
    return LineAt(position.line)->get(position.column);
  }

  // Returns the substring of the current line until the current position.
  shared_ptr<LazyString> current_line_head() const {
    return current_line()->Substring(0, current_position_col());
  }
  // Returns the substring of the current line from the current to the last
  // position.
  shared_ptr<LazyString> current_line_tail() const {
    return current_line()->Substring(current_position_col());
  }
  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  string ToString() const;

  void replace_current_line(const shared_ptr<Line>& line) {
    *line_ = line;
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
    return position().at_beginning();
  }
  bool at_beginning_of_line() const { return at_beginning_of_line(position()); }
  bool at_beginning_of_line(const LineColumn& position) const {
    if (contents_.empty()) { return true; }
    return position.at_beginning_of_line();
  }
  bool at_end() const { return at_end(position()); }
  bool at_end(const LineColumn& position) const {
    if (contents_.empty()) { return true; }
    return at_last_line(position) && at_end_of_line(position);
  }

  // Returns the position of just after the last character of the current file.
  LineColumn end_position() const {
    if (contents_.empty()) { return LineColumn(0, 0); }
    return LineColumn(contents_.size() - 1, (*contents_.rbegin())->size());
  }

  bool at_last_line() const { return at_last_line(position()); }
  bool at_last_line(const LineColumn& position) const {
    return position.line == contents_.size() - 1;
  }
  bool at_end_of_line() const {
    return at_end_of_line(position());
  }
  bool at_end_of_line(const LineColumn& position) const {
    if (contents_.empty()) { return true; }
    return position.column >= LineAt(position.line)->size();
  }
  char current_character() const {
    assert(current_position_col() < current_line()->size());
    return current_line()->get(current_position_col());
  }
  char previous_character() const {
    assert(current_position_col() > 0);
    return current_line()->get(current_position_col() - 1);
  }
  size_t current_position_line() const { return line_.line(); }
  void set_current_position_line(size_t value) {
    line_ = BufferLineIterator(this, value);
    set_bool_variable(variable_follow_end_of_file(), value >= contents_.size());
  }
  size_t current_position_col() const { return column_; }
  void set_current_position_col(size_t value) {
    column_ = value;
  }

  BufferLineIterator begin() {
    return BufferLineIterator(this, 0);
  }
  BufferLineIterator end() {
    auto const_result = const_cast<const OpenBuffer*>(this)->end();
    return BufferLineIterator(this, const_result.line());
  }
  const BufferLineConstIterator end() const {
    return BufferLineConstIterator(this, contents_.size());
  }
  BufferLineReverseIterator rbegin() {
    return BufferLineReverseIterator(end());
  }
  BufferLineReverseIterator rend() {
    return BufferLineReverseIterator(begin());
  }
  BufferLineIterator& line() {
    return line_;
  }

  void LineUp() {
    line()--;
    set_bool_variable(OpenBuffer::variable_follow_end_of_file(), false);
  }

  void LineDown() {
    line()++;
  }

  const LineColumn position() const {
    return LineColumn(line_.line(), column_);
  }
  void set_position(const LineColumn& position) {
    line_ = BufferLineIterator(this, position.line);
    set_bool_variable(variable_follow_end_of_file(),
                      position.line >= contents_.size());
    column_ = position.column;
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

  void PushSignal(EditorState* editor_state, int signal);

  void SetInputFile(int fd, bool fd_is_terminal, pid_t child_pid);

  void CopyVariablesFrom(const shared_ptr<const OpenBuffer>& buffer);

  static EdgeStruct<char>* BoolStruct();
  static EdgeVariable<char>* variable_pts();
  static EdgeVariable<char>* variable_vm_exec();
  static EdgeVariable<char>* variable_close_after_clean_exit();
  static EdgeVariable<char>* variable_reload_after_exit();
  static EdgeVariable<char>* variable_default_reload_after_exit();
  static EdgeVariable<char>* variable_reload_on_enter();
  static EdgeVariable<char>* variable_atomic_lines();
  static EdgeVariable<char>* variable_save_on_close();
  static EdgeVariable<char>* variable_clear_on_reload();
  static EdgeVariable<char>* variable_paste_mode();
  static EdgeVariable<char>* variable_follow_end_of_file();
  static EdgeVariable<char>* variable_commands_background_mode();
  static EdgeVariable<char>* variable_reload_on_buffer_write();

  static EdgeStruct<string>* StringStruct();
  static EdgeVariable<string>* variable_word_characters();
  static EdgeVariable<string>* variable_path_characters();
  static EdgeVariable<string>* variable_path();
  static EdgeVariable<string>* variable_pts_path();
  static EdgeVariable<string>* variable_command();
  static EdgeVariable<string>* variable_editor_commands_path();
  static EdgeVariable<string>* variable_line_prefix_characters();
  static EdgeVariable<string>* variable_line_suffix_superfluous_characters();

  static EdgeStruct<int>* IntStruct();
  static EdgeVariable<int>* variable_line_width();

  // No variables currently, but we'll likely add some later.
  static EdgeStruct<unique_ptr<Value>>* ValueStruct();

  bool read_bool_variable(const EdgeVariable<char>* variable) const;
  void set_bool_variable(const EdgeVariable<char>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<char>* variable);

  const string& read_string_variable(const EdgeVariable<string>* variable)
      const;
  void set_string_variable(const EdgeVariable<string>* variable,
                           const string& value);

  const int& read_int_variable(const EdgeVariable<int>* variable) const;
  void set_int_variable(const EdgeVariable<int>* variable,
                        const int& value);

  const Value* read_value_variable(
      const EdgeVariable<unique_ptr<Value>>* variable) const;
  void set_value_variable(const EdgeVariable<unique_ptr<Value>>* variable,
                          unique_ptr<Value> value);

  void Apply(EditorState* editor_state,
             unique_ptr<Transformation> transformation);
  void RepeatLastTransformation(EditorState* editor_state);

  void PushTransformationStack();
  void PopTransformationStack();
  bool HasTransformationStack() const {
    return !last_transformation_stack_.empty();
  }

  void Undo(EditorState* editor_state);

  void set_filter(unique_ptr<Value> filter);
  bool IsLineFiltered(size_t line);

  pid_t child_pid() const { return child_pid_; }
  int child_exit_status() const { return child_exit_status_; }

  size_t last_highlighted_line() const { return last_highlighted_line_; }
  void set_last_highlighted_line(size_t value) {
    last_highlighted_line_ = value;
  }

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
  size_t buffer_length_;
  size_t buffer_size_;
  // -1 means "no child process"
  pid_t child_pid_;
  int child_exit_status_;

  LineColumn position_pts_;

  vector<shared_ptr<Line>> contents_;

  size_t view_start_line_;
  size_t view_start_column_;

  BufferLineIterator line_;
  size_t column_;

  bool modified_;
  bool reading_from_parser_;

  // This uses char rather than bool because vector<bool>::iterator does not
  // yield a bool& when dereferenced, which makes EdgeStructInstance<bool>
  // incompatible with other template specializations
  // (EdgeStructInstance<bool>::Get would be returning a reference to a
  // temporary variable).
  EdgeStructInstance<char> bool_variables_;
  EdgeStructInstance<string> string_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<unique_ptr<Value>> function_variables_;

  // When a transformation is done, we append its result to
  // transformations_past_, so that it can be undone.
  list<unique_ptr<Transformation::Result>> transformations_past_;
  list<unique_ptr<Transformation::Result>> transformations_future_;

  Environment environment_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  unique_ptr<Value> filter_;
  size_t filter_version_;

 private:
  void ProcessCommandInput(
      EditorState* editor_state, shared_ptr<LazyString> str);
  unique_ptr<Transformation> last_transformation_;

  // We allow the user to group many transformations in one.  They each get
  // applied immediately, but upon repeating, the whole operation gets repeated.
  // This is controlled through OpenBuffer::PushTransformationStack, which sets
  // this to non-null (to signal that we've entered this mode) and
  // OpenBuffer::PopTransformationStack (which sets this back to null and moves
  // this value to last_transformation_).
  list<unique_ptr<TransformationStack>> last_transformation_stack_;

  // If variable_atomic_lines is true, this will be set to the last line that
  // was highlighted.
  size_t last_highlighted_line_;
};

}  // namespace editor
}  // namespace afc

#endif
