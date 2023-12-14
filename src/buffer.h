#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <glog/logging.h>

#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "src/buffer_name.h"
#include "src/buffer_state.h"
#include "src/buffer_syntax_parser.h"
#include "src/buffer_variables.h"
#include "src/concurrent/work_queue.h"
#include "src/editor_mode.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_adapter.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/screen/cursors.h"
#include "src/infrastructure/screen/visual_overlay.h"
#include "src/infrastructure/terminal_adapter.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/observers.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/line_marks.h"
#include "src/log.h"
#include "src/parse_tree.h"
#include "src/status.h"
#include "src/transformation.h"
#include "src/transformation/input.h"
#include "src/transformation/noop.h"
#include "src/transformation/type.h"
#include "src/undo_state.h"
#include "src/variables.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace afc {
namespace editor {
class ParseTree;
class TreeParser;
class BufferDisplayData;
class MapMode;
class MapModeCommands;
class UndoState;

class OpenBufferMutableLineSequenceObserver;

class OpenBuffer {
  struct ConstructorAccessTag {};

 public:
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
    // The returned future must be notified when the contents have been fully
    // loaded (for example, based on the return value of SetInputFiles).
    //
    // The caller (OpenBuffer) guarantees that the buffer won't be deleted until
    // the return future has received a value.
    std::function<futures::Value<language::PossibleError>(OpenBuffer&)>
        generate_contents = nullptr;

    // Optional function to generate additional information for the status of
    // this buffer (see OpenBuffer::FlagsString). The generated string must
    // begin with a space.
    std::function<std::map<std::wstring, std::wstring>(const OpenBuffer&)>
        describe_status = nullptr;

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
            infrastructure::Path edge_state_directory)>
        log_supplier = [](infrastructure::Path) {
          return futures::Past(language::Success(NewNullLog()));
        };
  };

  static language::gc::Root<OpenBuffer> New(Options options);
  OpenBuffer(ConstructorAccessTag, Options options,
             language::gc::Ptr<MapModeCommands> default_commands,
             language::gc::Ptr<InputReceiver> mode);
  ~OpenBuffer();

  EditorState& editor() const;

  // Set status information for this buffer. Only information specific to this
  // buffer should be set here; everything else should be set on the Editor's
  // status.
  Status& status() const;

  // If it is closeable, returns std::nullopt. Otherwise, returns reasons why
  // we can predict that PrepareToClose will fail.
  language::PossibleError IsUnableToPrepareToClose() const;

  // Starts saving this buffer.
  struct PrepareToCloseOutput {
    bool dirty_contents_saved_to_backup = false;
  };
  futures::ValueOrError<PrepareToCloseOutput> PrepareToClose();
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
  futures::Value<language::PossibleError> Save(Options::SaveType save_type);

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

  futures::Value<language::PossibleError> Reload();

  // If the buffer has a child process, sends EndOfFile to it.
  void SendEndOfFileToProcess();

  void ClearContents();
  void AppendEmptyLine();

  // Sort `length` lines starting at `start` according to a compare function.
  // `start` + `length` must be at most lines_size(). The line at position
  // `start` + `length` (if it exists) is not affected.
  void SortContents(language::text::LineNumber start,
                    language::text::LineNumberDelta length,
                    std::function<bool(const language::text::Line&,
                                       const language::text::Line&)>
                        compare);
  void SortAllContents(std::function<bool(const language::text::Line&,
                                          const language::text::Line&)>
                           compare);
  void SortAllContentsIgnoringCase();

  language::text::LineNumberDelta lines_size() const;
  language::text::LineNumber EndLine() const;

  InputReceiver& mode() const;
  language::gc::Root<InputReceiver> ResetMode();

  language::gc::Ptr<MapModeCommands> default_commands();

  // Erases all lines in range [first, last).
  void EraseLines(language::text::LineNumber first,
                  language::text::LineNumber last);

  // Inserts a new line into the buffer at a given position.
  void InsertLine(language::text::LineNumber line_position,
                  language::text::Line line);

  // Can handle \n characters, breaking it into lines.
  void AppendLazyString(language::lazy_string::LazyString input);
  // line must not contain \n characters.
  void AppendLine(language::lazy_string::LazyString line);
  void AppendRawLine(
      language::lazy_string::LazyString str,
      language::text::MutableLineSequence::ObserverBehavior observer_behavior =
          language::text::MutableLineSequence::ObserverBehavior::kShow);

  // Insert a line at the end of the buffer.
  void AppendRawLine(
      language::text::Line line,
      language::text::MutableLineSequence::ObserverBehavior observer_behavior =
          language::text::MutableLineSequence::ObserverBehavior::kShow);

  void AppendToLastLine(language::lazy_string::LazyString str);
  void AppendToLastLine(language::text::Line line);

  // Adds a new line. If there's a previous line, notifies various things about
  // it.
  void StartNewLine(language::text::Line line);
  // Equivalent to calling StartNewLine repeatedly, but significantly more
  // efficient.
  void AppendLines(
      std::vector<language::text::Line> lines,
      language::text::MutableLineSequence::ObserverBehavior observer_behavior =
          language::text::MutableLineSequence::ObserverBehavior::kShow);

  void DeleteRange(const language::text::Range& range);

  // If modifiers is present, applies it to every character (overriding the
  // modifiers from `insertion`; that is, from the input).
  language::text::LineColumn InsertInPosition(
      const language::text::LineSequence& contents_to_insert,
      const language::text::LineColumn& position,
      const std::optional<infrastructure::screen::LineModifierSet>& modifiers);

  // See MutableLineSequence::AdjustLineColumn.
  // TODO(trivial, 2023-08-24): Get rid of this method; switch all callers to
  // LineSequence.
  language::text::LineColumn AdjustLineColumn(
      language::text::LineColumn position) const;

  // If the current cursor is in a valid line (i.e., it isn't past the last
  // line), adjusts the column to not be beyond the length of the line.
  void MaybeAdjustPositionCol();
  // If the line referenced is shorter than the position.column, extend it with
  // spaces.
  void MaybeExtendLine(language::text::LineColumn position);

  // Makes sure that the current line (position) is not greater than the number
  // of elements in contents().  Note that after this, it may still not be a
  // valid index for contents() (it may be just at the end, perhaps because
  // contents() is empty).
  void CheckPosition();

  infrastructure::screen::CursorsSet& FindOrCreateCursors(
      const std::wstring& name);
  // May return nullptr.
  const infrastructure::screen::CursorsSet* FindCursors(
      const std::wstring& name) const;

  infrastructure::screen::CursorsSet& active_cursors();
  const infrastructure::screen::CursorsSet& active_cursors() const;

  // Removes all active cursors and replaces them with the ones given. The old
  // cursors are saved and can be restored with ToggleActiveCursors.
  void set_active_cursors(
      const std::vector<language::text::LineColumn>& positions);

  // Restores the last cursors available.
  void ToggleActiveCursors();
  void PushActiveCursors();
  void PopActiveCursors();
  // Replaces the set of active cursors with one cursor in every position with
  // a mark (based on line_marks_).
  void SetActiveCursorsToMarks();

  void set_current_cursor(language::text::LineColumn new_cursor);
  void CreateCursor();
  language::text::LineColumn FindNextCursor(language::text::LineColumn cursor,
                                            const Modifiers& modifiers);
  void DestroyCursor();
  void DestroyOtherCursors();

  language::text::Range FindPartialRange(
      const Modifiers& modifiers, language::text::LineColumn position) const;

  // Serializes the buffer into a string.  This is not particularly fast (it's
  // meant more for debugging/testing rather than for real use).
  std::wstring ToString() const;

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
  std::map<std::wstring, std::wstring> Flags() const;
  static language::lazy_string::LazyString FlagsToString(
      std::map<std::wstring, std::wstring> flags);

  futures::Value<std::wstring> TransformKeyboardText(std::wstring input);
  bool AddKeyboardTextTransformer(language::gc::Root<vm::Value> transformer);

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

  futures::Value<language::EmptyValue> Undo(
      UndoState::ApplyOptions::Mode undo_mode,
      UndoState::ApplyOptions::RedoMode redo_mode);

  void set_filter(language::gc::Root<vm::Value> filter);

  //////////////////////////////////////////////////////////////////////////////
  // Life cycle
  language::gc::Root<OpenBuffer> NewRoot();
  language::gc::Root<const OpenBuffer> NewRoot() const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

  //////////////////////////////////////////////////////////////////////////////
  // Line marks.

  // Returns a multimap with all the marks for the current buffer, indexed by
  // the line they refer to. Each call may update the map.
  const std::multimap<language::text::LineColumn, LineMarks::Mark>&
  GetLineMarks() const;
  const std::multimap<language::text::LineColumn, LineMarks::ExpiredMark>&
  GetExpiredLineMarks() const;
  std::wstring GetLineMarksText() const;

  /////////////////////////////////////////////////////////////////////////////
  // Extensions

  const language::gc::Ptr<vm::Environment>& environment() const {
    return environment_;
  }

  language::ValueOrError<
      std::pair<language::NonNull<std::unique_ptr<vm::Expression>>,
                language::gc::Root<vm::Environment>>>
  CompileString(const std::wstring& str) const;

  // `expr` can be deleted as soon as we return.
  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateExpression(
      const language::NonNull<std::shared_ptr<vm::Expression>>& expr,
      language::gc::Root<vm::Environment> environment);

  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateString(
      const std::wstring& str);
  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateFile(
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
  using LockFunction =
      std::function<void(language::OnceOnlyFunction<void(OpenBuffer&)>)>;
  LockFunction GetLockFunction();

  /////////////////////////////////////////////////////////////////////////////
  // Inspecting contents of buffer.

  // If the line is past the end of the file, returns the last available line
  // (as if MaybeAdjustPositionCol had been called).
  language::text::Line CurrentLine() const;

  // May return nullptr if the current_cursor is at the end of file.
  // TODO(trivial, 2023-12-14): Rename to CurrentLineOptional.
  std::optional<language::text::Line> CurrentLineOrNull() const;

  std::optional<language::text::Line> LineAt(
      language::text::LineNumber line_number) const;

  // We deliberately provide only a read view into our contents. All
  // modifications should be done through methods defined in this class.
  //
  // One exception to this is the TerminalInputParser class (to which we pass a
  // reference).
  //
  // TODO(easy, 2023-08-21): Stop passing a reference to TerminalInputParser;
  // instead, extend TerminalInputParser::Receiver.
  const language::text::MutableLineSequence& contents() const {
    return contents_;
  }

  BufferName name() const;

  /////////////////////////////////////////////////////////////////////////////
  // Interaction with the operating system

  // Returns a future that is notified when the two files provided have been
  // fully read.
  futures::Value<language::EmptyValue> SetInputFiles(
      infrastructure::FileDescriptor input_fd,
      infrastructure::FileDescriptor input_fd_error, bool fd_is_terminal,
      std::optional<infrastructure::ProcessId> child_pid);
  futures::Value<language::PossibleError> SetInputFromPath(
      const infrastructure::Path& path);

  const FileDescriptorReader* fd() const;
  const FileDescriptorReader* fd_error() const;

  void AddExecutionHandlers(
      infrastructure::execution::IterationHandler& handler);

  std::optional<infrastructure::ProcessId> child_pid() const;
  std::optional<int> child_exit_status() const { return child_exit_status_; }
  const struct timespec time_last_exit() const;

  void PushSignal(infrastructure::UnixSignal signal);

  infrastructure::FileSystemDriver& file_system_driver() const;

  language::NonNull<std::unique_ptr<infrastructure::TerminalAdapter>>
  NewTerminal();  // Public for testing.

  // Returns lines-read-per-second.
  double lines_read_rate() const;

  // Returns the path to the directory that should be used to keep state for the
  // current buffer. If the directory doesn't exist, creates it.
  futures::ValueOrError<infrastructure::Path> GetEdgeStateDirectory() const;

  Log& log() const;

  /////////////////////////////////////////////////////////////////////////////
  // Display
  BufferDisplayData& display_data();
  const BufferDisplayData& display_data() const;

  /////////////////////////////////////////////////////////////////////////////
  // Cursors

  const language::text::LineColumn position() const;
  void set_position(const language::text::LineColumn& position);

  enum class RemoteURLBehavior { kIgnore, kLaunchBrowser };
  futures::ValueOrError<std::optional<language::gc::Root<OpenBuffer>>>
  OpenBufferForCurrentPosition(RemoteURLBehavior remote_url_behavior);

  // Returns the position of just after the last character of the current file.
  language::text::LineColumn end_position() const;

  void set_current_position_line(language::text::LineNumber line);
  language::text::LineNumber current_position_line() const;
  language::lazy_string::ColumnNumber current_position_col() const;
  void set_current_position_col(language::lazy_string::ColumnNumber column);

  //////////////////////////////////////////////////////////////////////////////
  // Buffer variables

  const bool& Read(const EdgeVariable<bool>* variable) const;
  void Set(const EdgeVariable<bool>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<bool>* variable);

  const std::wstring& Read(const EdgeVariable<std::wstring>* variable) const;
  void Set(const EdgeVariable<std::wstring>* variable, std::wstring value);

  const int& Read(const EdgeVariable<int>* variable) const;
  void Set(const EdgeVariable<int>* variable, int value);

  const double& Read(const EdgeVariable<double>* variable) const;
  void Set(const EdgeVariable<double>* variable, double value);

  const language::text::LineColumn& Read(
      const EdgeVariable<language::text::LineColumn>* variable) const;
  void Set(const EdgeVariable<language::text::LineColumn>* variable,
           language::text::LineColumn value);

  //////////////////////////////////////////////////////////////////////////////
  // Parse tree

  language::NonNull<std::shared_ptr<const ParseTree>> parse_tree() const;
  language::NonNull<std::shared_ptr<const ParseTree>> simplified_parse_tree()
      const;

  size_t tree_depth() const { return tree_depth_; }
  void set_tree_depth(size_t tree_depth) { tree_depth_ = tree_depth; }

  const ParseTree& current_tree(const ParseTree& root) const;

  language::NonNull<std::shared_ptr<const ParseTree>>
  current_zoomed_out_parse_tree(language::text::LineNumberDelta lines) const;

  const infrastructure::screen::VisualOverlayMap& visual_overlay_map() const;
  // Returns the previous value.
  infrastructure::screen::VisualOverlayMap SetVisualOverlayMap(
      infrastructure::screen::VisualOverlayMap value);

 private:
  friend OpenBufferMutableLineSequenceObserver;

  // Code that would normally be in the constructor, but which may require the
  // use of `shared_from_this`. This function will be called by `New` after the
  // instance has been successfully installed into a std::shared_ptr.
  void Initialize(language::gc::Ptr<OpenBuffer> ptr_this);
  void MaybeStartUpdatingSyntaxTrees();

  futures::Value<transformation::Result> Apply(
      transformation::Variant transformation,
      language::text::LineColumn position, transformation::Input::Mode mode);
  void UpdateTreeParser();

  // TODO(trivial, 2023-12-11): Document why this isn't const-ref.
  void ProcessCommandInput(language::lazy_string::LazyString str);

  // Returns true if the position given is set to a value other than
  // language::text::LineColumn::Max and the buffer has read past that position.
  bool IsPastPosition(language::text::LineColumn position) const;

  void UpdateLastAction();

  // Signal that EndOfFile was received in both fd_ and fd_error_.
  void SignalEndOfFile();

  // TODO(P1, 2023-11-24): Remove this? Just use `AppendLines`?
  void InsertLines(std::vector<language::text::Line>);

  SeekInput NewSeekInput(Structure structure, Direction direction,
                         language::text::LineColumn* position) const;
  void OnCursorMove();
  void UpdateBackup();

  const Options options_;
  const language::NonNull<std::unique_ptr<transformation::Input::Adapter>>
      transformation_adapter_;

  language::NonNull<std::unique_ptr<Log>> log_ = NewNullLog();

  std::unique_ptr<FileDescriptorReader> fd_;
  std::unique_ptr<FileDescriptorReader> fd_error_;

  // Tracks lines read per second (through both fd_ and fd_error_).
  math::DecayingCounter lines_read_rate_ = math::DecayingCounter(2.0);

  // Non-const because Reload will reset it to a newly constructed instance.
  language::NonNull<std::unique_ptr<BufferDisplayData>> display_data_;

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
    // A reload is underway, but a new reload was requested. Once it's done, it
    // should switch to kUnderway and restart.
    kPending,
  };
  ReloadState reload_state_ = ReloadState::kDone;

  std::optional<infrastructure::ProcessId> child_pid_ = std::nullopt;
  std::optional<int> child_exit_status_;
  struct timespec time_last_exit_;
  // Optional function to execute when a sub-process exits.
  std::optional<language::OnceOnlyFunction<void()>> on_exit_handler_;

  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_ =
      concurrent::WorkQueue::New();

  infrastructure::screen::CursorsTracker cursors_tracker_;

  language::NonNull<std::shared_ptr<OpenBufferMutableLineSequenceObserver>>
      contents_observer_;

  language::text::MutableLineSequence contents_;
  infrastructure::screen::VisualOverlayMap visual_overlay_map_;

  DiskState disk_state_ = DiskState::kCurrent;
  DiskState backup_state_ = DiskState::kCurrent;
  bool reading_from_parser_ = false;

  BufferVariablesInstance variables_;

  UndoState undo_state_;

  std::list<language::gc::Root<vm::Value>> keyboard_text_transformers_;
  const language::gc::Ptr<vm::Environment> environment_;

  // A function that receives a string and returns a boolean. The function will
  // be evaluated on every line, to compute whether or not the line should be
  // shown.  This does not remove any lines: it merely hides them (by setting
  // the Line::filtered field).
  std::optional<language::gc::Root<vm::Value>> filter_;
  size_t filter_version_ = 0;

  transformation::Variant last_transformation_ = NewNoopTransformation();

  // We allow the user to group many transformations in one.  They each get
  // applied immediately, but upon repeating, the whole operation gets repeated.
  // This is controlled through OpenBuffer::PushTransformationStack, which sets
  // this to non-null (to signal that we've entered this mode) and
  // OpenBuffer::PopTransformationStack (which sets this back to null and moves
  // this value to last_transformation_).
  std::list<language::NonNull<std::unique_ptr<transformation::Stack>>>
      last_transformation_stack_;

  size_t tree_depth_ = 0;

  const language::gc::Ptr<MapModeCommands> default_commands_;
  language::gc::Ptr<InputReceiver> mode_;

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

  language::NonNull<std::unique_ptr<infrastructure::FileAdapter>> file_adapter_;

  mutable infrastructure::FileSystemDriver file_system_driver_;

  // Set by `Initialize`. Useful to retain references to this buffer (by turning
  // it into either a Root or WeakPtr).
  //
  // This isn't a Root because otherwise buffers would never be deallocated; it
  // also isn't a WeakPtr because ... if it is being accessed, we know the
  // containing object /must/ be alive.
  std::optional<language::gc::Ptr<OpenBuffer>> ptr_this_;
};

language::NonNull<std::unique_ptr<EditorState>> EditorForTests();
language::gc::Root<OpenBuffer> NewBufferForTests(EditorState& editor);
}  // namespace editor
}  // namespace afc

#endif
