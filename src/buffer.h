#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <glog/logging.h>

#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "src/buffer_contents.h"
#include "src/buffer_name.h"
#include "src/buffer_syntax_parser.h"
#include "src/buffer_terminal.h"
#include "src/concurrent/notification.h"
#include "src/concurrent/work_queue.h"
#include "src/cursors.h"
#include "src/file_descriptor_reader.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type.h"
#include "src/language/observers.h"
#include "src/lazy_string.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/line_marks.h"
#include "src/log.h"
#include "src/map_mode.h"
#include "src/parse_tree.h"
#include "src/status.h"
#include "src/substring.h"
#include "src/transformation.h"
#include "src/transformation/type.h"
#include "src/variables.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

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
class UnixSignal;

class OpenBuffer : public std::enable_shared_from_this<OpenBuffer> {
  struct ConstructorAccessTag {};

 public:
  static void RegisterBufferType(EditorState& editor_state,
                                 Environment* environment);

  struct Options {
    EditorState& editor;
    BufferName name = BufferName(L"");
    std::optional<infrastructure::Path> path = {};

    // Optional function that will be run to generate the contents of the
    // buffer.
    //
    // This will be run when the buffer is first created or when its contents
    // need to be reloaded.
    //
    // The returned future must be notified when the function is done executing.
    // This is typically used to offload to background threads IO operations.
    // When the future is notified, the contents are typically not yet ready:
    // they still need to be read. However, we'll know that `fd_` will have been
    // set at that point. That allows us to more correctly detect EOF.
    //
    // The caller (OpenBuffer) guarantees that the buffer won't be deleted until
    // the return future has received a value.
    std::function<futures::Value<language::PossibleError>(OpenBuffer&)>
        generate_contents = nullptr;

    // Optional function to generate additional information for the status of
    // this buffer (see OpenBuffer::FlagsString). The generated string must
    // begin with a space.
    std::function<map<wstring, wstring>(const OpenBuffer&)> describe_status =
        nullptr;

    // Optional function that listens on visits to the buffer (i.e., the user
    // entering the buffer from other buffers).
    std::function<void(OpenBuffer&)> handle_visit = nullptr;

    enum class SaveType { kMainFile, kBackup };
    // Optional function that saves the buffer. If not provided, attempts to
    // save the buffer will fail.
    struct HandleSaveOptions {
      OpenBuffer& buffer;
      SaveType save_type = SaveType::kMainFile;
    };
    std::function<futures::Value<language::PossibleError>(HandleSaveOptions)>
        handle_save = nullptr;

    std::function<
        futures::ValueOrError<language::NonNull<std::unique_ptr<Log>>>(
            std::shared_ptr<concurrent::WorkQueue> work_queue,
            infrastructure::Path edge_state_directory)>
        log_supplier =
            [](std::shared_ptr<concurrent::WorkQueue>, infrastructure::Path) {
              return futures::Past(language::Success(NewNullLog()));
            };
  };

  static language::NonNull<std::shared_ptr<OpenBuffer>> New(Options options);
  OpenBuffer(ConstructorAccessTag, Options options);
  ~OpenBuffer();

  EditorState& editor() const;

  // Set status information for this buffer. Only information specific to this
  // buffer should be set here; everything else should be set on the Editor's
  // status.
  Status& status() const;

  // If it is closeable, returns std::nullopt. Otherwise, returns reasons why
  // we can predict that PrepareToClose will fail.
  language::PossibleError IsUnableToPrepareToClose() const;

  // Starts saving this buffer. The future returned will have a value if there
  // was an error.
  futures::Value<language::PossibleError> PrepareToClose();
  void Close();

  // If the buffer was already read (fd_ == -1), this is immediately notified.
  // Otherwise, it'll be notified when the buffer is done being read.
  futures::Value<language::EmptyValue> WaitForEndOfFile();

  futures::Value<language::EmptyValue> NewCloseFuture();

  // Enter signals that the buffer went from being hidden to being displayed.
  void Enter();
  // Visit implies Enter but also signals that the buffer was actively selected
  // (rather than just temporarily shown).
  void Visit();

  struct timespec last_visit() const;
  struct timespec last_action() const;

  // Saves state of this buffer (not including contents). Currently that means
  // the values of variables, but in the future it could include other things.
  // Returns true if the state could be persisted successfully.
  futures::Value<language::PossibleError> PersistState() const;

  // If an error occurs, returns it (in the future). Otherwise, returns an
  // empty value.
  futures::Value<language::PossibleError> Save();

  // If we're currently at the end of the buffer *and* variable
  // `follow_end_of_file` is set, returns an object that, when deleted, will
  // move the position to the end of file.
  //
  // If variable `pts` is set, has slightly different semantics: the end
  // position will not be the end of contents(), but rather position_pts_.
  std::unique_ptr<bool, std::function<void(bool*)>> GetEndPositionFollower();

  bool ShouldDisplayProgress() const;
  void RegisterProgress();
  struct timespec last_progress_update() const { return last_progress_update_; }

  void ReadData();
  void ReadErrorData();

  void Reload();
  // Signal that EndOfFile was received (in the input that the editor is reading
  // from fd_).
  void EndOfFile();

  // If the buffer has a child process, sends EndOfFile to it.
  void SendEndOfFileToProcess();

  void ClearContents(BufferContents::CursorsBehavior cursors_behavior);
  void AppendEmptyLine();

  // Sort all lines in range [first, last) according to a compare function.
  void SortContents(
      LineNumber first, LineNumber last,
      std::function<bool(const language::NonNull<shared_ptr<const Line>>&,
                         const language::NonNull<shared_ptr<const Line>>&)>
          compare);

  LineNumberDelta lines_size() const;
  LineNumber EndLine() const;

  EditorMode* mode() const { return mode_.get(); }
  language::NonNull<std::shared_ptr<EditorMode>> ResetMode() {
    auto copy = std::move(mode_);
    mode_ = language::MakeNonNullShared<MapMode>(default_commands_);
    return copy;
  }

  // Erases all lines in range [first, last).
  void EraseLines(LineNumber first, LineNumber last);

  // Inserts a new line into the buffer at a given position.
  void InsertLine(LineNumber line_position,
                  language::NonNull<std::shared_ptr<Line>> line);

  // Can handle \n characters, breaking it into lines.
  void AppendLazyString(language::NonNull<std::shared_ptr<LazyString>> input);
  // line must not contain \n characters.
  void AppendLine(language::NonNull<std::shared_ptr<LazyString>> line);
  void AppendRawLine(language::NonNull<std::shared_ptr<LazyString>> str);

  // Insert a line at the end of the buffer.
  void AppendRawLine(language::NonNull<std::shared_ptr<Line>> line);

  void AppendToLastLine(language::NonNull<std::shared_ptr<LazyString>> str);
  void AppendToLastLine(Line line);

  // Adds a new line. If there's a previous line, notifies various things about
  // it.
  void StartNewLine(language::NonNull<std::shared_ptr<const Line>> line);
  // Equivalent to calling StartNewLine repeatedly, but significantly more
  // efficient.
  void AppendLines(
      std::vector<language::NonNull<std::shared_ptr<const Line>>> lines);

  void DeleteRange(const Range& range);

  // If modifiers is present, applies it to every character (overriding the
  // modifiers from `insertion`; that is, from the input).
  LineColumn InsertInPosition(const BufferContents& contents_to_insert,
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

  CursorsSet* FindOrCreateCursors(const wstring& name);
  // May return nullptr.
  const CursorsSet* FindCursors(const wstring& name) const;

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
  LineColumn FindNextCursor(LineColumn cursor, const Modifiers& modifiers);
  void DestroyCursor();
  void DestroyOtherCursors();

  Range FindPartialRange(const Modifiers& modifiers, LineColumn position) const;

  // If there's a buffer associated with the current line (looking up the
  // "buffer" variable in the line's environment), returns it. Returns nullptr
  // otherwise.
  std::shared_ptr<OpenBuffer> GetBufferFromCurrentLine();

  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  wstring ToString() const;

  enum class DiskState {
    // The file (in disk) reflects our last changes.
    kCurrent,
    // The file (in disk) doesn't reflect the last changes applied by the user.
    kStale
  };
  void SetDiskState(DiskState disk_state) { disk_state_ = disk_state; }
  DiskState disk_state() const { return disk_state_; }
  // Returns a unique_ptr with the current disk state value. When the pointer is
  // destroyed, restores the state of the buffer to that value. This is useful
  // for customers that want to apply modifications to the buffer that shouldn't
  // be reflected in the disk state (for example, because these modifications
  // come from disk).
  std::unique_ptr<DiskState, std::function<void(DiskState*)>> FreezeDiskState();

  bool dirty() const;
  std::map<wstring, wstring> Flags() const;
  static wstring FlagsToString(std::map<wstring, wstring> flags);

  futures::Value<std::wstring> TransformKeyboardText(std::wstring input);
  bool AddKeyboardTextTransformer(unique_ptr<Value> transformer);

  futures::Value<language::EmptyValue> ApplyToCursors(
      transformation::Variant transformation);
  futures::Value<language::EmptyValue> ApplyToCursors(
      transformation::Variant transformation,
      Modifiers::CursorsAffected cursors_affected,
      transformation::Input::Mode mode);
  futures::Value<language::EmptyValue> RepeatLastTransformation();

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
  futures::Value<language::EmptyValue> Undo(UndoMode undo_mode);

  void set_filter(unique_ptr<Value> filter);

  // Returns a multimap with all the marks for the current buffer, indexed by
  // the line they refer to. Each call may update the map.
  const multimap<size_t, LineMarks::Mark>& GetLineMarks() const;
  wstring GetLineMarksText() const;

  /////////////////////////////////////////////////////////////////////////////
  // Extensions

  const std::shared_ptr<Environment>& environment() const {
    return environment_;
  }

  language::ValueOrError<
      std::pair<language::NonNull<std::unique_ptr<Expression>>,
                std::shared_ptr<Environment>>>
  CompileString(const wstring& str);

  // `expr` can be deleted as soon as we return.
  futures::ValueOrError<language::NonNull<std::unique_ptr<Value>>>
  EvaluateExpression(Expression& expr,
                     std::shared_ptr<Environment> environment);

  futures::ValueOrError<language::NonNull<std::unique_ptr<Value>>>
  EvaluateString(const wstring& str);
  futures::ValueOrError<language::NonNull<std::unique_ptr<Value>>> EvaluateFile(
      const infrastructure::Path& path);

  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>>& work_queue()
      const;

  // Asynchronous threads that need to interact with the buffer shouldn't be
  // given a direct reference to the buffer, since OpenBuffer isn't thread safe.
  // Instead, they should receive a `LockFunction`: calling the `LockFunction`
  // with a callback will schedule that callback for execution in the main
  // thread, from which access to the OpenBuffer instance is safe.
  //
  // Typically a call to a `LockFunction` will return before the callback given
  // is executed.
  //
  // Retaining the LockFunction ensures that the underlying buffer is retained.
  using LockFunction = std::function<void(std::function<void(OpenBuffer&)>)>;
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
  const BufferContents& contents() const { return contents_; }

  BufferName name() const;

  /////////////////////////////////////////////////////////////////////////////
  // Interaction with the operating system

  void SetInputFiles(infrastructure::FileDescriptor input_fd,
                     infrastructure::FileDescriptor input_fd_error,
                     bool fd_is_terminal, pid_t child_pid);

  const FileDescriptorReader* fd() const;
  const FileDescriptorReader* fd_error() const;

  pid_t child_pid() const { return child_pid_; }
  std::optional<int> child_exit_status() const { return child_exit_status_; }
  const struct timespec time_last_exit() const;

  void PushSignal(UnixSignal signal);

  language::ObservableValue<LineColumnDelta>& view_size();
  const language::ObservableValue<LineColumnDelta>& view_size() const;

  infrastructure::FileSystemDriver& file_system_driver() const;

  // Returns the path to the directory that should be used to keep state for the
  // current buffer. If the directory doesn't exist, creates it.
  futures::ValueOrError<infrastructure::Path> GetEdgeStateDirectory() const;

  Log& log() const;

  void UpdateBackup();

  /////////////////////////////////////////////////////////////////////////////
  // Cursors

  const LineColumn position() const;
  void set_position(const LineColumn& position);

  // Can return nullptr.
  enum class RemoteURLBehavior { kIgnore, kLaunchBrowser };
  futures::ValueOrError<std::shared_ptr<OpenBuffer>>
  OpenBufferForCurrentPosition(RemoteURLBehavior remote_url_behavior);

  // Returns the position of just after the last character of the current file.
  LineColumn end_position() const;

  void set_current_position_line(LineNumber line);
  LineNumber current_position_line() const;
  ColumnNumber current_position_col() const;
  void set_current_position_col(ColumnNumber column);

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

  const LineColumn& Read(const EdgeVariable<LineColumn>* variable) const;
  void Set(const EdgeVariable<LineColumn>* variable, LineColumn value);

  //////////////////////////////////////////////////////////////////////////////
  // Parse tree

  language::NonNull<std::shared_ptr<const ParseTree>> parse_tree() const;
  language::NonNull<std::shared_ptr<const ParseTree>> simplified_parse_tree()
      const;

  size_t tree_depth() const { return tree_depth_; }
  void set_tree_depth(size_t tree_depth) { tree_depth_ = tree_depth; }

  const ParseTree* current_tree(const ParseTree* root) const;

  language::NonNull<std::shared_ptr<const ParseTree>>
  current_zoomed_out_parse_tree(LineNumberDelta lines) const;

  std::unique_ptr<BufferTerminal> NewTerminal();  // Public for testing.

 private:
  // Code that would normally be in the constructor, but which may require the
  // use of `shared_from_this`. This function will be called by `New` after the
  // instance has been successfully installed into a shared_ptr.
  void Initialize();
  void MaybeStartUpdatingSyntaxTrees();

  futures::Value<transformation::Result> Apply(
      transformation::Variant transformation, LineColumn position,
      transformation::Input::Mode mode);
  void UpdateTreeParser();

  void ProcessCommandInput(shared_ptr<LazyString> str);

  // Returns true if the position given is set to a value other than
  // LineColumn::Max and the buffer has read past that position.
  bool IsPastPosition(LineColumn position) const;

  // Reads from one of the two FileDescriptorReader instances in the buffer
  // (i.e., `fd_` or `fd_error_`).
  void ReadData(std::unique_ptr<FileDescriptorReader>& source);

  void UpdateLastAction();

  const Options options_;

  language::NonNull<std::unique_ptr<Log>> log_ = NewNullLog();

  std::unique_ptr<FileDescriptorReader> fd_;
  std::unique_ptr<FileDescriptorReader> fd_error_;

  language::ObservableValue<LineColumnDelta> view_size_;

  std::unique_ptr<BufferTerminal> terminal_;

  // Functions to be called when the end of file is reached. The functions will
  // be called at most once (so they won't be notified if the buffer is
  // reloaded.
  language::Observers end_of_file_observers_;

  // Functions to call when this buffer is deleted.
  language::Observers close_observers_;

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

  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;

  BufferContents contents_;

  DiskState disk_state_ = DiskState::kCurrent;
  DiskState backup_state_ = DiskState::kCurrent;
  bool reading_from_parser_ = false;

  EdgeStructInstance<bool> bool_variables_;
  EdgeStructInstance<wstring> string_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<double> double_variables_;
  EdgeStructInstance<LineColumn> line_column_variables_;

  // When a transformation is done, we append its result to
  // transformations_past_, so that it can be undone.
  std::list<std::shared_ptr<transformation::Stack>> undo_past_;
  std::list<std::shared_ptr<transformation::Stack>> undo_future_;

  std::list<unique_ptr<Value>> keyboard_text_transformers_;
  const std::shared_ptr<Environment> environment_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  std::unique_ptr<Value> filter_;
  size_t filter_version_;

  transformation::Variant last_transformation_;

  // We allow the user to group many transformations in one.  They each get
  // applied immediately, but upon repeating, the whole operation gets repeated.
  // This is controlled through OpenBuffer::PushTransformationStack, which sets
  // this to non-null (to signal that we've entered this mode) and
  // OpenBuffer::PopTransformationStack (which sets this back to null and moves
  // this value to last_transformation_).
  std::list<std::unique_ptr<transformation::Stack>> last_transformation_stack_;

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

  const language::NonNull<std::shared_ptr<MapModeCommands>> default_commands_;
  language::NonNull<std::shared_ptr<EditorMode>> mode_;

  // The time when the buffer was last selected as active.
  struct timespec last_visit_ = {};
  // The time when the buffer last saw some action. This includes being visited,
  // receiving input and probably other things.
  struct timespec last_action_ = {};

  // The time when variable_progress was last incremented.
  //
  // TODO: Add a Time type to the VM and expose this?
  struct timespec last_progress_update_ = {0, 0};

  mutable Status status_;

  BufferSyntaxParser buffer_syntax_parser_;

  mutable infrastructure::FileSystemDriver file_system_driver_;
};

EditorState& EditorForTests();
language::NonNull<std::shared_ptr<OpenBuffer>> NewBufferForTests();
}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::OpenBuffer>> {
  static std::shared_ptr<editor::OpenBuffer> get(Value* value);
  static language::NonNull<Value::Ptr> New(
      std::shared_ptr<editor::OpenBuffer> value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif
