#ifndef __AFC_EDITOR_EDITOR_H__
#define __AFC_EDITOR_EDITOR_H__

#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/args.h"
#include "src/audio.h"
#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/buffer_widget.h"
#include "src/buffers_list.h"
#include "src/command_mode.h"
#include "src/concurrent/thread_pool.h"
#include "src/concurrent/work_queue.h"
#include "src/direction.h"
#include "src/editor_mode.h"
#include "src/editor_variables.h"
#include "src/language/ghost_type.h"
#include "src/lazy_string.h"
#include "src/line_marks.h"
#include "src/modifiers.h"
#include "src/status.h"
#include "src/transformation.h"
#include "src/transformation/type.h"
#include "src/widget.h"
#include "src/widget_list.h"
#include "vm/public/environment.h"
#include "vm/public/vm.h"

namespace afc::editor {

GHOST_TYPE(UnixSignal, int);

using namespace afc::vm;

using std::list;
using std::map;
using std::max;
using std::min;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;

enum class CommandArgumentModeApplyMode;

class EditorState {
 public:
  struct ScreenState {
    bool needs_hard_redraw = false;
  };

  EditorState(CommandLineValues args, audio::Player& audio_player);
  ~EditorState();

  const bool& Read(const EdgeVariable<bool>* variable) const;
  void Set(const EdgeVariable<bool>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<bool>* variable);
  const wstring& Read(const EdgeVariable<wstring>* variable) const;
  void Set(const EdgeVariable<wstring>* variable, wstring value);
  const int& Read(const EdgeVariable<int>* variable) const;
  void Set(const EdgeVariable<int>* variable, int value);
  const double& Read(const EdgeVariable<double>* variable) const;
  void Set(const EdgeVariable<double>* variable, double value);

  void CheckPosition();

  void CloseBuffer(OpenBuffer& buffer);

  const std::map<BufferName, std::shared_ptr<OpenBuffer>>* buffers() const {
    return &buffers_;
  }

  std::map<BufferName, std::shared_ptr<OpenBuffer>>* buffers() {
    return &buffers_;
  }
  BuffersList& buffer_tree() { return buffer_tree_; }
  const BuffersList& buffer_tree() const { return buffer_tree_; }

  void set_current_buffer(shared_ptr<OpenBuffer> buffer,
                          CommandArgumentModeApplyMode apply_mode);
  void AddHorizontalSplit();
  void AddVerticalSplit();
  void SetActiveBuffer(size_t position);
  void AdvanceActiveBuffer(int delta);
  void AdjustWidgets();

  bool has_current_buffer() const;
  shared_ptr<OpenBuffer> current_buffer();
  const shared_ptr<OpenBuffer> current_buffer() const;
  // Returns the set of buffers that should be modified by commands.
  std::vector<std::shared_ptr<OpenBuffer>> active_buffers() const;
  void AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                 BuffersList::AddBufferType insertion_type);
  futures::Value<language::EmptyValue> ForEachActiveBuffer(
      std::function<futures::Value<language::EmptyValue>(OpenBuffer&)>
          callback);
  // Similar to ForEachActiveBuffer, but if repetions are set, only runs the
  // callback for the buffer referenced by repetitions (in the list of buffers,
  // buffer_tree_).
  futures::Value<language::EmptyValue> ForEachActiveBufferWithRepetitions(
      std::function<futures::Value<language::EmptyValue>(OpenBuffer&)>
          callback);

  // Convenience wrapper of `ForEachActiveBuffer` that applies `transformation`.
  futures::Value<language::EmptyValue> ApplyToActiveBuffers(
      transformation::Variant transformation);

  BufferName GetUnusedBufferName(const wstring& prefix);
  std::optional<int> exit_value() const { return exit_value_; }

  enum class TerminationType { kWhenClean, kIgnoringErrors };
  void Terminate(TerminationType termination_type, int exit_value);

  void ResetModifiers() {
    auto buffer = current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
    modifiers_.ResetSoft();
  }

  Direction direction() const { return modifiers_.direction; }
  void set_direction(Direction direction) { modifiers_.direction = direction; }
  void ResetDirection() { modifiers_.ResetDirection(); }
  Direction default_direction() const { return modifiers_.default_direction; }
  void set_default_direction(Direction direction) {
    modifiers_.default_direction = direction;
    ResetDirection();
  }

  std::optional<size_t> repetitions() const { return modifiers_.repetitions; }
  void ResetRepetitions() { modifiers_.ResetRepetitions(); }
  void set_repetitions(size_t value) { modifiers_.repetitions = value; }

  Modifiers modifiers() const { return modifiers_; }
  void set_modifiers(const Modifiers& modifiers) { modifiers_ = modifiers; }

  Structure* structure() const { return modifiers_.structure; }
  void set_structure(Structure* structure) { modifiers_.structure = structure; }
  void ResetStructure() { modifiers_.ResetStructure(); }

  bool sticky_structure() const { return modifiers_.sticky_structure; }
  void set_sticky_structure(bool sticky_structure) {
    modifiers_.sticky_structure = sticky_structure;
  }

  Modifiers::ModifyMode insertion_modifier() const {
    return modifiers_.insertion;
  }
  void set_insertion_modifier(Modifiers::ModifyMode insertion_modifier) {
    modifiers_.insertion = insertion_modifier;
  }
  void ResetInsertionModifier() { modifiers_.ResetInsertion(); }
  Modifiers::ModifyMode default_insertion_modifier() const {
    return modifiers_.default_insertion;
  }
  void set_default_insertion_modifier(
      Modifiers::ModifyMode default_insertion_modifier) {
    modifiers_.default_insertion = default_insertion_modifier;
  }

  futures::Value<language::EmptyValue> ProcessInputString(const string& input);
  futures::Value<language::EmptyValue> ProcessInput(int c);

  const LineMarks& line_marks() const { return line_marks_; }
  LineMarks& line_marks() { return line_marks_; }

  std::shared_ptr<MapModeCommands> default_commands() const {
    return default_commands_;
  }

  void MoveBufferForwards(size_t times);
  void MoveBufferBackwards(size_t times);

  // Returns nullptr if the redraw should be skipped.
  std::optional<ScreenState> FlushScreenState();
  void set_screen_needs_hard_redraw(bool value) {
    screen_state_.needs_hard_redraw = value;
  }

  void PushCurrentPosition();
  void PushPosition(LineColumn position);
  std::shared_ptr<OpenBuffer> GetConsole();
  bool HasPositionsInStack();
  BufferPosition ReadPositionsStack();
  bool MovePositionsStack(Direction direction);

  Status& status();
  const Status& status() const;

  const infrastructure::Path& home_directory() const { return home_directory_; }
  const vector<infrastructure::Path>& edge_path() const { return edge_path_; }

  std::shared_ptr<Environment> environment() { return environment_; }

  infrastructure::Path expand_path(infrastructure::Path path) const;

  void PushSignal(UnixSignal signal);
  void ProcessSignals();
  void StartHandlingInterrupts() { handling_interrupts_ = true; }
  bool handling_interrupts() const { return handling_interrupts_; }
  bool handling_stop_signals() const;

  infrastructure::FileDescriptor fd_to_detect_internal_events() const {
    return pipe_to_communicate_internal_events_.first;
  }

  audio::Player& audio_player() const { return audio_player_; }

  // Can return null.
  std::shared_ptr<EditorMode> keyboard_redirect() const {
    return keyboard_redirect_;
  }
  // Returns the old value.
  std::shared_ptr<EditorMode> set_keyboard_redirect(
      std::shared_ptr<EditorMode> keyboard_redirect) {
    keyboard_redirect_.swap(keyboard_redirect);
    return keyboard_redirect;
  }

  // Executes pending work from all buffers.
  void ExecutePendingWork();
  std::optional<struct timespec> WorkQueueNextExecution() const;
  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>>& work_queue()
      const;
  concurrent::ThreadPool& thread_pool();

  void ResetInternalEventNotifications();

 private:
  void NotifyInternalEvent();

  std::shared_ptr<Environment> BuildEditorEnvironment();

  EdgeStructInstance<wstring> string_variables_;
  EdgeStructInstance<bool> bool_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<double> double_variables_;

  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;
  concurrent::ThreadPool thread_pool_;

  std::map<BufferName, std::shared_ptr<OpenBuffer>> buffers_;
  std::optional<int> exit_value_;

  const infrastructure::Path home_directory_;
  const std::vector<infrastructure::Path> edge_path_;

  double frames_per_second_;

  const std::shared_ptr<Environment> environment_;

  // Should only be directly used when the editor has no buffer.
  std::shared_ptr<MapModeCommands> default_commands_;
  std::shared_ptr<EditorMode> keyboard_redirect_;

  // Used to honor command line argument frames_per_second. Holds the earliest
  // time when a redraw should be allowed.
  struct timespec next_screen_update_ = {0, 0};
  ScreenState screen_state_;

  // Initially we don't consume SIGINT: we let it crash the process (in case
  // the user has accidentally ran Edge). However, as soon as the user starts
  // actually using Edge (e.g. modifies a buffer), we start consuming it.
  bool handling_interrupts_ = false;

  vector<UnixSignal> pending_signals_;

  Modifiers modifiers_;
  LineMarks line_marks_;

  // Each editor has a pipe. The customer of the editor can poll from the read
  // end, to detect the need to redraw the screen. Internally, background
  // threads write to the write end to trigger that.
  //
  // We use has_internal_events_ to avoid redundantly notifying this. The
  // customer must call ResetInternalEventsNotifications to reset it just before
  // starting to process events.
  const std::pair<infrastructure::FileDescriptor,
                  infrastructure::FileDescriptor>
      pipe_to_communicate_internal_events_;
  concurrent::Protected<bool> has_internal_events_ =
      concurrent::Protected<bool>(false);

  audio::Player& audio_player_;

  BuffersList buffer_tree_;
  Status status_;
};

}  // namespace afc::editor

#endif
