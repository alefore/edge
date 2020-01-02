#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <glog/logging.h>

#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "src/async_processor.h"
#include "src/buffer_contents.h"
#include "src/buffer_terminal.h"
#include "src/cursors.h"
#include "src/file_descriptor_reader.h"
#include "src/futures/futures.h"
#include "src/lazy_string.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/line_marks.h"
#include "src/map_mode.h"
#include "src/status.h"
#include "src/substring.h"
#include "src/transformation.h"
#include "src/variables.h"
#include "src/viewers.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"
#include "src/work_queue.h"

namespace afc {
namespace editor {

using std::iterator;
using std::list;
using std::map;
using std::max;
using std::min;
using std::multimap;
using std::ostream;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

using namespace afc::vm;

class ParseTree;
class TreeParser;

class OpenBuffer {
 public:
  // Name of a special buffer that shows the list of buffers.
  static const wstring kBuffersName;
  static const wstring kPasteBuffer;

  static void RegisterBufferType(EditorState* editor_state,
                                 Environment* environment);

  struct Options {
    EditorState* editor;
    wstring name;
    wstring path;

    // Optional function that will be run to generate the contents of the
    // buffer.
    //
    // This will be run when the buffer is first created or when its contents
    // need to be reloaded.
    std::function<void(OpenBuffer*)> generate_contents;

    // Optional function to generate additional information for the status of
    // this buffer (see OpenBuffer::FlagsString). The generated string must
    // begin with a space.
    std::function<map<wstring, wstring>(const OpenBuffer&)> describe_status;

    // Optional function that listens on visits to the buffer (i.e., the user
    // entering the buffer from other buffers).
    std::function<void(OpenBuffer*)> handle_visit;

    // Optional function that saves the buffer. If not provided, attempts to
    // save the buffer will fail.
    std::function<void(OpenBuffer*)> handle_save;
  };

  OpenBuffer(EditorState* editor_state, const wstring& name);
  OpenBuffer(Options options);
  ~OpenBuffer();

  EditorState* editor() const;

  // Set status information for this buffer. Only information specific to this
  // buffer should be set here; everything else should be set on the Editor's
  // status.
  Status* status() const;

  // If it is closeable, returns std::nullopt. Otherwise, returns reasons why
  // we can predict that PrepareToClose will fail.
  std::optional<wstring> IsUnableToPrepareToClose() const;

  // Starts saving this buffer. When done, calls either `success` or `failure`.
  // These callbacks may not run if a new call to `PrepareToClose` is made; in
  // other words, typically only the last values passed will run.
  void PrepareToClose(std::function<void()> success,
                      std::function<void(wstring)> failure);
  void Close();

  // If the buffer is still being read (fd_ != -1), adds an observer to
  // end_of_file_observers_. Otherwise just calls the observer directly.
  void AddEndOfFileObserver(std::function<void()> observer);
  void AddCloseObserver(std::function<void()> observer);

  void Visit();
  time_t last_visit() const;
  time_t last_action() const;

  // Saves state of this buffer (not including contents). Currently that means
  // the values of variables, but in the future it could include other things.
  // Returns true if the state could be persisted successfully.
  bool PersistState() const;

  void Save();

  // If we're currently at the end of the buffer *and* variable
  // `follow_end_of_file` is set, returns an object that, when deleted, will
  // move the position to the end of file.
  //
  // If variable `pts` is set, has slightly different semantics: the end
  // position will not be the end of contents(), but rather position_pts_.
  std::unique_ptr<bool, std::function<void(bool*)>> GetEndPositionFollower();

  bool ShouldDisplayProgress() const;
  void RegisterProgress();
  struct timespec last_progress_update() const {
    return last_progress_update_;
  }

  void ReadData();
  void ReadErrorData();

  void Reload();
  void EndOfFile();

  void ClearContents(BufferContents::CursorsBehavior cursors_behavior);
  void AppendEmptyLine();

  // Sort all lines in range [first, last) according to a compare function.
  void SortContents(LineNumber first, LineNumber last,
                    std::function<bool(const shared_ptr<const Line>&,
                                       const shared_ptr<const Line>&)>
                        compare);

  LineNumberDelta lines_size() const;
  LineNumber EndLine() const;

  EditorMode* mode() const { return mode_.get(); }
  std::shared_ptr<EditorMode> ResetMode() {
    auto copy = std::move(mode_);
    mode_.reset(new MapMode(default_commands_));
    return copy;
  }
  void set_mode(std::shared_ptr<EditorMode> mode) { mode_ = std::move(mode); }

  // Erases all lines in range [first, last).
  void EraseLines(LineNumber first, LineNumber last);

  // Inserts a new line into the buffer at a given position.
  void InsertLine(LineNumber line_position, shared_ptr<Line> line);

  // Can handle \n characters, breaking it into lines.
  void AppendLazyString(std::shared_ptr<LazyString> input);
  // line must not contain \n characters.
  void AppendLine(std::shared_ptr<LazyString> line);
  void AppendRawLine(std::shared_ptr<LazyString> str);

  // Insert a line at the end of the buffer.
  void AppendRawLine(std::shared_ptr<Line> line);

  void AppendToLastLine(std::shared_ptr<LazyString> str);
  void AppendToLastLine(Line line);

  // Adds a new line. If there's a previous line, notifies various things about
  // it.
  void StartNewLine(std::shared_ptr<Line> line);

  void DeleteRange(const Range& range);

  // If modifiers is present, applies it to every character (overriding the
  // modifiers from `insertion`; that is, from the input).
  LineColumn InsertInPosition(const OpenBuffer& insertion,
                              const LineColumn& position,
                              const std::optional<LineModifierSet>& modifiers);
  // Returns a copy of position, but ensuring that it is in the expected range
  // (i.e., that the line is valid, and that the column fits the length of the
  // line).
  LineColumn AdjustLineColumn(LineColumn position) const;

  // If the current cursor is in a valid line (i.e., it isn't past the last
  // line), adjusts the column to not be beyond the length of the line.
  void MaybeAdjustPositionCol();
  // If the line referenced is shorter than the position.column, extend it with
  // spaces.
  void MaybeExtendLine(LineColumn position);

  // Makes sure that the current line (position) is not greater than the number
  // of elements in contents().  Note that after this, it may still not be a
  // valid index for contents() (it may be just at the end, perhaps because
  // contents() is empty).
  void CheckPosition();

  CursorsSet* FindCursors(const wstring& name);
  CursorsSet* active_cursors();
  const CursorsSet* active_cursors() const;

  // Removes all active cursors and replaces them with the ones given. The old
  // cursors are saved and can be restored with ToggleActiveCursors.
  void set_active_cursors(const vector<LineColumn>& positions);

  // Restores the last cursors available.
  void ToggleActiveCursors();
  void PushActiveCursors();
  void PopActiveCursors();
  // Replaces the set of active cursors with one cursor in every position with
  // a mark (based on line_marks_).
  void SetActiveCursorsToMarks();

  void set_current_cursor(LineColumn new_cursor);
  void CreateCursor();
  LineColumn FindNextCursor(LineColumn cursor);
  void DestroyCursor();
  void DestroyOtherCursors();

  Range FindPartialRange(const Modifiers& modifiers, LineColumn position);

  // If there's a buffer associated with the current line (looking up the
  // "buffer" variable in the line's environment), returns it. Returns nullptr
  // otherwise.
  std::shared_ptr<OpenBuffer> GetBufferFromCurrentLine();

  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  wstring ToString() const;

  void ClearModified() { modified_ = false; }
  bool modified() const { return modified_; }

  bool dirty() const;
  std::map<wstring, wstring> Flags() const;
  static wstring FlagsToString(std::map<wstring, wstring> flags);

  wstring TransformKeyboardText(wstring input);
  bool AddKeyboardTextTransformer(unique_ptr<Value> transformer);

  futures::DelayedValue<bool> ApplyToCursors(
      unique_ptr<Transformation> transformation);
  futures::DelayedValue<bool> ApplyToCursors(
      unique_ptr<Transformation> transformation,
      Modifiers::CursorsAffected cursors_affected,
      Transformation::Input::Mode mode);
  futures::DelayedValue<bool> RepeatLastTransformation();

  void PushTransformationStack();
  void PopTransformationStack();
  bool HasTransformationStack() const {
    return !last_transformation_stack_.empty();
  }

  enum class UndoMode {
    // Iterate the history, undoing transformations, until the buffer is
    // actually modified.
    kLoop,
    // Only undo the last transformation (whether or not that causes any
    // modifications).
    kOnlyOne
  };
  void Undo(UndoMode undo_mode);

  void set_filter(unique_ptr<Value> filter);
  bool IsLineFiltered(LineNumber line);

  // Returns a multimap with all the marks for the current buffer, indexed by
  // the line they refer to. Each call may update the map.
  const multimap<size_t, LineMarks::Mark>* GetLineMarks() const;
  wstring GetLineMarksText() const;

  /////////////////////////////////////////////////////////////////////////////
  // Extensions

  Environment* environment() { return &environment_; }

  unique_ptr<Expression> CompileString(const wstring& str,
                                       wstring* error_description);
  void EvaluateExpression(Expression* expr,
                          std::function<void(std::unique_ptr<Value>)> consumer);
  bool EvaluateString(const wstring& str,
                      std::function<void(std::unique_ptr<Value>)> consumer);
  bool EvaluateFile(const wstring& path,
                    std::function<void(std::unique_ptr<Value>)> consumer);

  WorkQueue* work_queue();
  void ExecutePendingWork();
  WorkQueue::State GetPendingWorkState() const;

  // Asynchronous threads that need to interact with the buffer shouldn't be
  // given a direct reference to the buffer, since OpenBuffer isn't thread safe.
  // Instead, they should receive a `LockFunction`: calling the `LockFunction`
  // with a callback will schedule that callback for execution in the main
  // thread, from which access to the OpenBuffer instance is safe.
  //
  // Typically a call to a `LockFunction` will return before the callback given
  // is executed.
  using LockFunction = std::function<void(std::function<void(OpenBuffer*)>)>;
  LockFunction GetLockFunction();

  /////////////////////////////////////////////////////////////////////////////
  // Inspecting contents of buffer.

  // May return nullptr if the current_cursor is at the end of file.
  const shared_ptr<const Line> current_line() const;

  shared_ptr<const Line> LineAt(LineNumber line_number) const;

  // We deliberately provide only a read view into our contents. All
  // modifications should be done through methods defined in this class.
  //
  // One exception to this is the BufferTerminal class (to which we pass a
  // reference).
  const BufferContents* contents() const { return &contents_; }

  /////////////////////////////////////////////////////////////////////////////
  // Interaction with the operating system

  void SetInputFiles(int input_fd, int input_fd_error, bool fd_is_terminal,
                     pid_t child_pid);

  const FileDescriptorReader* fd() const;
  const FileDescriptorReader* fd_error() const;

  pid_t child_pid() const { return child_pid_; }
  std::optional<int> child_exit_status() const { return child_exit_status_; }
  const struct timespec time_last_exit() const;

  void PushSignal(int signal);

  Viewers* viewers();

  /////////////////////////////////////////////////////////////////////////////
  // Cursors

  const LineColumn position() const;
  void set_position(const LineColumn& position);

  // Returns the position of just after the last character of the current file.
  LineColumn end_position() const;

  void set_current_position_line(LineNumber line);
  LineNumber current_position_line() const;
  ColumnNumber current_position_col() const;
  void set_current_position_col(ColumnNumber column);

  LineColumn PositionBefore(LineColumn position) const;

  //////////////////////////////////////////////////////////////////////////////
  // Buffer variables

  const bool& Read(const EdgeVariable<bool>* variable) const;
  void Set(const EdgeVariable<bool>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<bool>* variable);

  const wstring& Read(const EdgeVariable<wstring>* variable) const;
  void Set(const EdgeVariable<wstring>* variable, wstring value);

  const int& Read(const EdgeVariable<int>* variable) const;
  void Set(const EdgeVariable<int>* variable, int value);

  const double& Read(const EdgeVariable<double>* variable) const;
  void Set(const EdgeVariable<double>* variable, double value);

  //////////////////////////////////////////////////////////////////////////////
  // Parse tree

  // Never returns nullptr (may return an empty tree instead).
  std::shared_ptr<const ParseTree> parse_tree() const;
  // Never returns nullptr (may return an empty tree instead).
  std::shared_ptr<const ParseTree> simplified_parse_tree() const;

  size_t tree_depth() const { return tree_depth_; }
  void set_tree_depth(size_t tree_depth) { tree_depth_ = tree_depth; }

  const ParseTree* current_tree(const ParseTree* root) const;

  std::shared_ptr<const ParseTree> current_zoomed_out_parse_tree(
      LineNumberDelta lines) const;

  std::unique_ptr<BufferTerminal> NewTerminal();  // Public for testing.

 private:
  struct SyntaxDataInput {
    std::unique_ptr<const BufferContents> contents;
    std::shared_ptr<TreeParser> parser;
  };
  struct SyntaxDataOutput {
    std::shared_ptr<const ParseTree> parse_tree;
    std::shared_ptr<const ParseTree> simplified_parse_tree;
    std::map<LineNumberDelta, std::shared_ptr<const ParseTree>>
        zoomed_out_parse_trees;
  };

  static void EvaluateMap(OpenBuffer* buffer, LineNumber line,
                          Value::Callback map_callback,
                          TransformationStack* transformation,
                          Trampoline* trampoline);
  static SyntaxDataOutput UpdateSyntaxData(SyntaxDataInput input);

  // Given a simplified_parse_tree and a desired view size, computes the zoomed
  // out parse tree.
  struct SyntaxDataZoomInput {
    const OpenBuffer* buffer;
    // Total number of lines in the buffer.
    LineNumberDelta lines_size;
    LineNumberDelta view_size;
    std::shared_ptr<const ParseTree> simplified_parse_tree;
  };
  struct SyntaxDataZoomOutput {
    std::shared_ptr<const ParseTree> simplified_parse_tree;
    std::shared_ptr<const ParseTree> zoomed_parse_tree;
  };
  static int UpdateSyntaxDataZoom(SyntaxDataZoomInput input);

  futures::DelayedValue<Transformation::Result> Apply(
      unique_ptr<Transformation> transformation, LineColumn position,
      Transformation::Input::Mode mode);
  void UpdateTreeParser();

  void ProcessCommandInput(shared_ptr<LazyString> str);

  // Returns true if the position given is set to a value other than
  // LineColumn::Max and the buffer has read past that position.
  bool IsPastPosition(LineColumn position) const;

  // Reads from one of the two FileDescriptorReader instances in the buffer
  // (i.e., `fd_` or `fd_error_`).
  void ReadData(std::unique_ptr<FileDescriptorReader>* source);

  const Options options_;

  std::unique_ptr<FileDescriptorReader> fd_;
  std::unique_ptr<FileDescriptorReader> fd_error_;

  Viewers viewers_;

  std::unique_ptr<BufferTerminal> terminal_;

  // Functions to be called when the end of file is reached. The functions will
  // be called at most once (so they won't be notified if the buffer is
  // reloaded.
  vector<std::function<void()>> end_of_file_observers_;

  // Functions to call when this buffer is deleted.
  std::vector<std::function<void()>> close_observers_;

  enum class ReloadState {
    // No need to reload this buffer.
    kDone,
    // A reload is currently ongoing. If it finishes in this state, we switch to
    // kDone.
    kOngoing,
    // A reload is underway, but a new reload was requested. Once it's done,
    // it should switch to kUnderway and restart.
    kPending,
  };
  ReloadState reload_state_ = ReloadState::kDone;

  // 0 means "no child process"
  pid_t child_pid_ = -1;
  std::optional<int> child_exit_status_;
  struct timespec time_last_exit_;
  // Optional function to execute when a sub-process exits.
  std::function<void()> on_exit_handler_;

  BufferContents contents_;

  bool modified_ = false;
  bool reading_from_parser_ = false;

  EdgeStructInstance<bool> bool_variables_;
  EdgeStructInstance<wstring> string_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<double> double_variables_;

  // When a transformation is done, we append its result to
  // transformations_past_, so that it can be undone.
  std::list<std::unique_ptr<TransformationStack>> undo_past_;
  std::list<std::unique_ptr<TransformationStack>> undo_future_;

  list<unique_ptr<Value>> keyboard_text_transformers_;
  Environment environment_;

  mutable WorkQueue work_queue_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  unique_ptr<Value> filter_;
  size_t filter_version_;

  unique_ptr<Transformation> last_transformation_;

  // We allow the user to group many transformations in one.  They each get
  // applied immediately, but upon repeating, the whole operation gets repeated.
  // This is controlled through OpenBuffer::PushTransformationStack, which sets
  // this to non-null (to signal that we've entered this mode) and
  // OpenBuffer::PopTransformationStack (which sets this back to null and moves
  // this value to last_transformation_).
  list<unique_ptr<TransformationStack>> last_transformation_stack_;

  // Index of the marks for the current buffer (i.e. Mark::target_buffer is the
  // current buffer). The key is the line (i.e. Mark::line).
  //
  // mutable because GetLineMarks will always update it if needed.
  mutable multimap<size_t, LineMarks::Mark> line_marks_;
  // The value that EditorState::marks_::updates had when we last computed
  // line_marks_. This allows us to avoid recomputing line_marks_ when no new
  // marks have been added.
  mutable size_t line_marks_last_updates_ = 0;

  CursorsTracker cursors_tracker_;

  size_t tree_depth_ = 0;

  std::shared_ptr<MapModeCommands> default_commands_;
  std::shared_ptr<EditorMode> mode_;

  // The time when the buffer was last selected as active.
  time_t last_visit_ = 0;
  // The time when the buffer last saw some action. This includes being visited,
  // receiving input and probably other things.
  time_t last_action_ = 0;

  // The time when variable_progress was last incremented.
  //
  // TODO: Add a Time type to the VM and expose this?
  struct timespec last_progress_update_ = {0, 0};

  mutable Status status_;

  enum class SyntaxDataState {
    // We need to schedule an update in syntax_data_. When we set
    // syntax_data_state_ to kPending, we schedule into `pending_work_` a
    // callback to trigger the update (through the background thread in
    // `syntax_data_`).
    kPending,
    // We've already scheduled the last update in syntax_data_. It may not yet
    // be fully computed (it may be currently being computing in the background
    // thread of syntax_data_), but we don't have anything else to do.
    kDone
  };
  SyntaxDataState syntax_data_state_ = SyntaxDataState::kDone;
  std::shared_ptr<TreeParser> tree_parser_;
  AsyncProcessor<SyntaxDataInput, SyntaxDataOutput> syntax_data_;
  mutable AsyncProcessor<SyntaxDataZoomInput, int> syntax_data_zoom_;
  // Caches the last parse done (by syntax_data_zoom_) for a given view size.
  mutable std::unordered_map<LineNumberDelta, SyntaxDataZoomOutput>
      zoomed_out_parse_trees_;

  BackgroundCallbackRunner background_read_runner_ =
      NewBackgroundCallbackRunner(L"BackgroundReadRunner");
};

}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::OpenBuffer>> {
  static std::shared_ptr<editor::OpenBuffer> get(Value* value);
  static Value::Ptr New(std::shared_ptr<editor::OpenBuffer> value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif
