#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <cassert>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "lazy_string.h"
#include "line.h"
#include "line_column.h"
#include "line_marks.h"
#include "memory_mapped_file.h"
#include "parse_tree.h"
#include "substring.h"
#include "transformation.h"
#include "tree.h"
#include "variables.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"
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

class OpenBuffer {
 public:
  typedef std::multiset<LineColumn> CursorsSet;

  // Name of a special buffer that shows the list of buffers.
  static const wstring kBuffersName;
  static const wstring kPasteBuffer;

  static void RegisterBufferType(EditorState* editor_state,
                                 Environment* environment);

  OpenBuffer(EditorState* editor_state, const wstring& name);
  ~OpenBuffer();

  bool PrepareToClose(EditorState* editor_state);
  void Close(EditorState* editor_state);

  // If the buffer is still being read (fd_ != -1), adds an observer to
  // end_of_file_observers_. Otherwise just calls the observer directly.
  void AddEndOfFileObserver(std::function<void()> observer);

  virtual void Enter(EditorState* editor_state);

  void ClearContents(EditorState* editor_state);
  void AppendEmptyLine(EditorState* editor_state);

  virtual void ReloadInto(EditorState*, OpenBuffer*) {}
  virtual void Save(EditorState* editor_state);

  void MaybeFollowToEndOfFile();

  // Returns the position immediately after end or before start (depending on
  // the direction modifier).
  LineColumn MovePosition(const Modifiers& modifiers, LineColumn start,
                          LineColumn end);

  void ReadData(EditorState* editor_state);
  void ReadErrorData(EditorState* editor_state);

  void Reload(EditorState* editor_state);
  virtual void EndOfFile(EditorState* editor_state);

  // Sort all lines in range [first, last) according to a compare function.
  void SortContents(
      const Tree<shared_ptr<Line>>::const_iterator& first,
      const Tree<shared_ptr<Line>>::const_iterator& last,
      std::function<bool(const shared_ptr<Line>&, const shared_ptr<Line>&)>
          compare);

  Tree<shared_ptr<Line>>::const_iterator EraseLines(
      Tree<shared_ptr<Line>>::const_iterator first,
      Tree<shared_ptr<Line>>::const_iterator last);

  // Overwrites the line at a given position with a new line.
  void ReplaceLine(Tree<shared_ptr<Line>>::const_iterator position,
                   shared_ptr<Line> line);

  // Inserts a new line into the buffer at a given position.
  void InsertLine(
      Tree<shared_ptr<Line>>::const_iterator position,
      shared_ptr<Line> line);

  void AppendLazyString(EditorState* editor_state, shared_ptr<LazyString> input);
  void AppendLine(EditorState* editor_state, shared_ptr<LazyString> line);
  virtual void AppendRawLine(EditorState* editor_state, shared_ptr<LazyString> str);

  // Insert a line at the end of the buffer.
  void AppendRawLine(EditorState* editor, shared_ptr<Line> line);

  size_t ProcessTerminalEscapeSequence(
      EditorState* editor_state, shared_ptr<LazyString> str, size_t read_index,
      std::unordered_set<Line::Modifier, hash<int>>* modifiers);
  void AppendToLastLine(EditorState* editor_state, shared_ptr<LazyString> str);
  void AppendToLastLine(
      EditorState* editor_state, shared_ptr<LazyString> str,
      const vector<unordered_set<Line::Modifier, hash<int>>>& modifiers);

  unique_ptr<Expression> CompileString(EditorState* editor_state,
                                       const wstring& str,
                                       wstring* error_description);
  unique_ptr<Value> EvaluateExpression(EditorState* editor_state,
                                       Expression* expr);
  unique_ptr<Value> EvaluateString(EditorState* editor_state,
                                   const wstring& str);
  unique_ptr<Value> EvaluateFile(EditorState* editor_state,
                                 const wstring& path);

  const wstring& name() const { return name_; }

  LineColumn InsertInCurrentPosition(const Tree<shared_ptr<Line>>& insertion);
  LineColumn InsertInPosition(
      const Tree<shared_ptr<Line>>& insertion, const LineColumn& position);
  // Checks that line column is in the expected range (between 0 and the length
  // of the current line).
  void AdjustLineColumn(LineColumn* output) const;

  // Like AdjustLineColumn but for the current cursor.
  void MaybeAdjustPositionCol();

  // Makes sure that the current line (position) is not greater than the number
  // of elements in contents().  Note that after this, it may still not be a
  // valid index for contents() (it may be just at the end).
  void CheckPosition();

  std::map<std::wstring, CursorsSet>* cursors() { return &cursors_; }
  CursorsSet* FindCursors(const wstring& name);
  CursorsSet* active_cursors();
  // Removes all active cursors and replaces them with the ones given. The old
  // cursors are saved and can be restored with ToggleActiveCursors.
  void set_active_cursors(const vector<LineColumn>& positions);

  // Restores the last cursors available.
  void ToggleActiveCursors();

  void AdjustCursors(std::function<LineColumn(LineColumn)> callback);
  void set_current_cursor(CursorsSet::value_type new_cursor);
  CursorsSet::iterator current_cursor();
  CursorsSet::const_iterator current_cursor() const;
  void CreateCursor();
  CursorsSet::iterator FindPreviousCursor(LineColumn cursor);
  CursorsSet::iterator FindNextCursor(LineColumn cursor);
  void DestroyCursor();
  void DestroyOtherCursors();

  const ParseTree* current_tree() const;

  struct TreeSearchResult {
    // The parent containing the tree for the depth given at the position given.
    const ParseTree* parent;
    // The index of the children of parent for the tree.
    size_t index;
    // The depth of the parent.
    size_t depth;
  };

  TreeSearchResult FindTreeInPosition(size_t depth, const LineColumn& position,
                                      Direction direction) const;

  // Returns the index of the first children of tree that ends in a position
  // greater than the one given.
  size_t FindChildrenForPosition(const ParseTree* tree,
                                 const LineColumn& position,
                                 Direction direction) const;

  bool FindRangeFirst(
    const Modifiers& modifiers, const LineColumn& position,
    LineColumn* output) const;
  bool FindRangeLast(
    const Modifiers& modifiers, const LineColumn& position,
    LineColumn* output) const;

  // Sets the positions pointed to by start and end to the beginning and end of
  // the structure at the position given.
  //
  // You probably want to call FindPartialRange instead.
  bool FindRange(const Modifiers& modifiers, const LineColumn& position,
                 LineColumn* first, LineColumn* last);

  // Same as FindRange, but honors Modifiers::structure_range.
  bool FindPartialRange(
    const Modifiers& modifiers, const LineColumn& position, LineColumn* start,
    LineColumn* end);

  // May return nullptr if the current_cursor is at the end of file.
  const shared_ptr<Line> current_line() const;
  shared_ptr<Line> current_line();

  shared_ptr<Line> LineAt(size_t line_number) const {
    CHECK(!contents_.empty());
    if (line_number >= contents_.size()) {
      return nullptr;
    }
    return contents_.at(line_number);
  }
  char character_at(LineColumn position) const {
    auto line = LineAt(position.line);
    if (line->size() > position.column) {
      return line->get(position.column);
    } else {
      return L'\n';
    }
  }

  // If there's a buffer associated with the current line (looking up the
  // "buffer" variable in the line's environment), returns it. Returns nullptr
  // otherwise.
  std::shared_ptr<OpenBuffer> GetBufferFromCurrentLine();

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
  wstring ToString() const;

  void replace_current_line(const shared_ptr<Line>& line) {
    if (current_cursor_->line >= contents_.size()) {
      CHECK(!contents_.empty());
      set_current_position_line(contents_.size() - 1);
    }
    contents_[current_cursor_->line] = line;
  }

  int fd() const { return fd_.fd; }
  int fd_error() const { return fd_error_.fd; }

  // We deliberately provide only a read view into our contents. All
  // modifications should be done through methods defined in this class.
  const Tree<shared_ptr<Line>>* contents() const { return &contents_; }

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

  void set_current_position_line(size_t line);
  size_t current_position_line() const;
  size_t current_position_col() const;
  void set_current_position_col(size_t column);

  const LineColumn position() const;

  void set_position(const LineColumn& position);

  void set_modified(bool value) { modified_ = value; }
  bool modified() const { return modified_; }

  bool dirty() const {
    return modified_
        || child_pid_ != -1
        || !WIFEXITED(child_exit_status_)
        || WEXITSTATUS(child_exit_status_) != 0;
  }
  wstring FlagsString() const;

  void PushSignal(EditorState* editor_state, int signal);

  wstring TransformKeyboardText(wstring input);
  bool AddKeyboardTextTransformer(EditorState* editor_state,
                                  unique_ptr<Value> transformer);

  void SetInputFiles(
      EditorState* editor_state, int input_fd, int input_fd_error,
      bool fd_is_terminal, pid_t child_pid);

  void CopyVariablesFrom(const shared_ptr<const OpenBuffer>& buffer);

  static EdgeStruct<char>* BoolStruct();
  static EdgeVariable<char>* variable_pts();
  static EdgeVariable<char>* variable_vm_exec();
  static EdgeVariable<char>* variable_close_after_clean_exit();
  static EdgeVariable<char>* variable_allow_dirty_delete();
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
  static EdgeVariable<char>* variable_contains_line_marks();
  static EdgeVariable<char>* variable_multiple_cursors();
  static EdgeVariable<char>* variable_reload_on_display();
  static EdgeVariable<char>* variable_show_in_buffers_list();
  static EdgeVariable<char>* variable_push_positions_to_history();
  static EdgeVariable<char>* variable_delete_into_paste_buffer();

  static EdgeStruct<wstring>* StringStruct();
  static EdgeVariable<wstring>* variable_word_characters();
  static EdgeVariable<wstring>* variable_path_characters();
  static EdgeVariable<wstring>* variable_path();
  static EdgeVariable<wstring>* variable_pts_path();
  static EdgeVariable<wstring>* variable_command();
  static EdgeVariable<wstring>* variable_editor_commands_path();
  static EdgeVariable<wstring>* variable_line_prefix_characters();
  static EdgeVariable<wstring>* variable_line_suffix_superfluous_characters();
  static EdgeVariable<wstring>* variable_dictionary();
  static EdgeVariable<wstring>* variable_tree_parser();

  static EdgeStruct<int>* IntStruct();
  static EdgeVariable<int>* variable_line_width();
  static EdgeVariable<int>* variable_buffer_list_context_lines();

  // No variables currently, but we'll likely add some later.
  static EdgeStruct<unique_ptr<Value>>* ValueStruct();

  bool read_bool_variable(const EdgeVariable<char>* variable) const;
  void set_bool_variable(const EdgeVariable<char>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<char>* variable);

  const wstring& read_string_variable(const EdgeVariable<wstring>* variable)
      const;
  void set_string_variable(const EdgeVariable<wstring>* variable,
                           const wstring& value);

  const int& read_int_variable(const EdgeVariable<int>* variable) const;
  void set_int_variable(const EdgeVariable<int>* variable,
                        const int& value);

  const Value* read_value_variable(
      const EdgeVariable<unique_ptr<Value>>* variable) const;
  void set_value_variable(const EdgeVariable<unique_ptr<Value>>* variable,
                          unique_ptr<Value> value);

  void ApplyToCursors(unique_ptr<Transformation> transformation);
  LineColumn Apply(EditorState* editor_state,
                   unique_ptr<Transformation> transformation,
                   LineColumn cursor);
  void RepeatLastTransformation();

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

  // Returns a multimap with all the marks for the current buffer, indexed by
  // the line they refer to. Each call may update the map.
  const multimap<size_t, LineMarks::Mark>*
      GetLineMarks(const EditorState& editor_state);

  Environment* environment() { return &environment_; }

  ParseTree* parse_tree() { return &parse_tree_; }

  size_t tree_depth() const { return tree_depth_; }
  void set_tree_depth(size_t tree_depth) { tree_depth_ = tree_depth; }

  std::shared_ptr<bool> BlockParseTreeUpdates();

  LineColumn PositionBefore(LineColumn position) const {
    if (contents_.empty()) {
      position = LineColumn();
    } else if (position.column > 0) {
      position.column--;
    } else if (position.line > 0) {
      position.line = min(position.line - 1, contents_.size() - 1);
      position.column = contents_.at(position.line)->size();
    }
    return position;
  }

 protected:
  EditorState* editor_;

  wstring name_;

  struct Input {
    void Close();
    void Reset();
    void ReadData(EditorState* editor_state, OpenBuffer* target);

    // -1 means "no file descriptor" (i.e. not currently loading this).
    int fd = -1;

    // We read directly into low_buffer_ and then drain from that into contents_.
    // It's possible that not all bytes read can be converted (for example, if the
    // reading stops in the middle of a wide character).
    char* low_buffer = nullptr;
    size_t low_buffer_length = 0;

    unordered_set<Line::Modifier, hash<int>> modifiers;
  };

  Input fd_;
  Input fd_error_;

  // This is used to remember if we obtained a terminal for the file descriptor
  // (for a subprocess).  Typically this has the same value of pts_ after a
  // subprocess is started, but it's a separate value to allow the user to
  // change the value of pts_ without breaking things (when one command is
  // already running).
  bool fd_is_terminal_ = false;

  // Functions to be called when the end of file is reached. The functions will
  // be called at most once (so they won't be notified if the buffer is
  // reloaded.
  vector<std::function<void()>> end_of_file_observers_;

  // -1 means "no child process"
  pid_t child_pid_;
  int child_exit_status_;

  LineColumn position_pts_;

  Tree<shared_ptr<Line>> contents_;

  size_t view_start_line_;
  size_t view_start_column_;

  bool modified_;
  bool reading_from_parser_;

  // This uses char rather than bool because vector<bool>::iterator does not
  // yield a bool& when dereferenced, which makes EdgeStructInstance<bool>
  // incompatible with other template specializations
  // (EdgeStructInstance<bool>::Get would be returning a reference to a
  // temporary variable).
  EdgeStructInstance<char> bool_variables_;
  EdgeStructInstance<wstring> string_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<unique_ptr<Value>> function_variables_;

  // When a transformation is done, we append its result to
  // transformations_past_, so that it can be undone.
  list<unique_ptr<Transformation::Result>> transformations_past_;
  list<unique_ptr<Transformation::Result>> transformations_future_;

  list<unique_ptr<Value>> keyboard_text_transformers_;
  Environment environment_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  unique_ptr<Value> filter_;
  size_t filter_version_;

 private:
  void UpdateTreeParser();
  void ResetParseTree();

  // Adds a new line. If there's a previous line, notifies various things about
  // it.
  void StartNewLine(EditorState* editor_state);
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

  // Index of the marks for the current buffer (i.e. Mark::target_buffer is the
  // current buffer). The key is the line (i.e. Mark::line).
  multimap<size_t, LineMarks::Mark> line_marks_;
  // The value of EditorState::marks_::updates the last time we computed
  // line_marks_. This allows us to avoid recomputing it when no new marks have
  // been added.
  size_t line_marks_last_updates_ = 0;

  // Contains a collection of positions that commands should be applied to.
  std::map<std::wstring, CursorsSet> cursors_;

  CursorsSet::iterator current_cursor_;

  // While we're applying a transformation to a set of cursors, we need to
  // remember what cursors it has already been applied to. To do that, we
  // gradually drain the original set of cursors and add them here as we apply
  // the transformation to them. We can't just loop over the set of cursors
  // since each transformation will likely reshuffle them. Once the source of
  // cursors to modify is empty, we just swap it back with this.
  CursorsSet already_applied_cursors_;

  // If we get a request to open a buffer and jump to a given line, we store
  // that value here. Once we've read enough lines, we stay at this position.
  size_t desired_line_ = 0;

  std::unique_ptr<TreeParser> tree_parser_;
  ParseTree parse_tree_;
  size_t tree_depth_ = 0;

  // When a caller wants to make multiple modifications and ensure that the
  // update of the parse_tree_ only happens at the end, they should use
  // BlockParseTreeUpdates() to obtain a pointer; no updates will take place
  // while the pointer is held. When the pointer is released, the tree will be
  // updated (unless other callers are also blocking updates). This allows
  // multiple callers to block parsing, having it resume as soon as all drop
  // their pointers. Callers must never keep these pointers longer than the life
  // of their corresponding OpenBuffer.
  std::weak_ptr<bool> block_parse_tree_updates_;
  // Set to true if some tree update is blocked by block_parse_tree_updates_.
  // That way, once the operations are done, we can avoid re-parsing when it's
  // not needed.
  bool pending_parse_tree_updates_ = false;
};

}  // namespace editor
}  // namespace afc

#endif
