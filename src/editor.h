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
#include "src/infrastructure/audio.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/insert_history.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/once_only_function.h"
#include "src/line_marks.h"
#include "src/modifiers.h"
#include "src/status.h"
#include "src/transformation.h"
#include "src/transformation/type.h"
#include "src/widget.h"
#include "src/widget_list.h"

namespace afc::vm {
class Environment;
}
namespace afc::editor {

class Buffercontents;
class BufferRegistry;
enum class CommandArgumentModeApplyMode;

class EditorState {
  const CommandLineValues args_;

  struct SharedData {
    // Each editor has a pipe. The customer of the editor can poll from the read
    // end, to detect the need to redraw the screen. Internally, background
    // threads write to the write end to trigger that.
    //
    // We use has_internal_events to avoid redundantly notifying this. The
    // customer must call ResetInternalEventsNotifications to reset it just
    // before starting to process events.
    std::pair<infrastructure::FileDescriptor, infrastructure::FileDescriptor>
        pipe_to_communicate_internal_events;
    concurrent::Protected<bool> has_internal_events =
        concurrent::Protected<bool>(false);
    Status status;
  };

  // The callbacks we schedule in work_queue_ may survive `this`. Anything those
  // callbacks may depend on will be in `shared_data_`, allowing `this` to be
  // deleted.
  const language::NonNull<std::shared_ptr<SharedData>> shared_data_;

 public:
  struct ScreenState {
    bool needs_hard_redraw = false;
  };

  EditorState(CommandLineValues args,
              infrastructure::audio::Player& audio_player);
  ~EditorState();

  const CommandLineValues& args();

  const bool& Read(const EdgeVariable<bool>* variable) const;
  void Set(const EdgeVariable<bool>* variable, bool value);
  void toggle_bool_variable(const EdgeVariable<bool>* variable);
  const std::wstring& Read(const EdgeVariable<std::wstring>* variable) const;
  void Set(const EdgeVariable<std::wstring>* variable, std::wstring value);
  const int& Read(const EdgeVariable<int>* variable) const;
  void Set(const EdgeVariable<int>* variable, int value);
  const double& Read(const EdgeVariable<double>* variable) const;
  void Set(const EdgeVariable<double>* variable, double value);

  void CheckPosition();

  void CloseBuffer(OpenBuffer& buffer);

  const std::map<BufferName, afc::language::gc::Root<OpenBuffer>>* buffers()
      const {
    return &buffers_;
  }

  std::map<BufferName, afc::language::gc::Root<OpenBuffer>>* buffers() {
    return &buffers_;
  }

  language::gc::Root<OpenBuffer> FindOrBuildBuffer(
      BufferName,
      language::OnceOnlyFunction<language::gc::Root<OpenBuffer>()> factory);

  BuffersList& buffer_tree() { return buffer_tree_; }
  const BuffersList& buffer_tree() const { return buffer_tree_; }

  void set_current_buffer(language::gc::Root<OpenBuffer> buffer,
                          CommandArgumentModeApplyMode apply_mode);
  void AddHorizontalSplit();
  void AddVerticalSplit();
  void SetActiveBuffer(size_t position);
  void AdvanceActiveBuffer(int delta);
  void AdjustWidgets();

  bool has_current_buffer() const;
  std::optional<language::gc::Root<OpenBuffer>> current_buffer() const;
  // Returns the set of buffers that should be modified by commands.
  std::vector<language::gc::Root<OpenBuffer>> active_buffers() const;
  void AddBuffer(language::gc::Root<OpenBuffer> buffer,
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

  void set_exit_value(int exit_value);
  std::optional<int> exit_value() const { return exit_value_; }
  std::optional<language::lazy_string::LazyString> GetExitNotice() const;

  enum class TerminationType { kWhenClean, kIgnoringErrors };
  void Terminate(TerminationType termination_type, int exit_value);

  void ResetModifiers();

  Direction direction() const;
  void set_direction(Direction direction);
  void ResetDirection();
  Direction default_direction() const;
  void set_default_direction(Direction direction);

  BufferRegistry& buffer_registry();
  const BufferRegistry& buffer_registry() const;

  std::optional<size_t> repetitions() const;
  void ResetRepetitions();
  void set_repetitions(size_t value);

  Modifiers modifiers() const;
  void set_modifiers(const Modifiers& modifiers);

  Structure structure() const;
  void set_structure(Structure structure);
  void ResetStructure();

  bool sticky_structure() const;
  void set_sticky_structure(bool sticky_structure);

  Modifiers::ModifyMode insertion_modifier() const;

  void set_insertion_modifier(Modifiers::ModifyMode insertion_modifier);

  void ResetInsertionModifier();
  Modifiers::ModifyMode default_insertion_modifier() const;

  void set_default_insertion_modifier(
      Modifiers::ModifyMode default_insertion_modifier);

  futures::Value<language::EmptyValue> ProcessInput(
      const std::vector<infrastructure::ExtendedChar>& input);

  const LineMarks& line_marks() const { return line_marks_; }
  LineMarks& line_marks() { return line_marks_; }

  const language::gc::Root<MapModeCommands>& default_commands() const {
    return default_commands_;
  }

  // Returns nullptr if the redraw should be skipped.
  std::optional<ScreenState> FlushScreenState();
  void set_screen_needs_hard_redraw(bool value) {
    screen_state_.needs_hard_redraw = value;
  }

  void PushCurrentPosition();
  void PushPosition(language::text::LineColumn position);
  language::gc::Root<OpenBuffer> GetConsole();
  bool HasPositionsInStack();
  BufferPosition ReadPositionsStack();
  bool MovePositionsStack(Direction direction);

  Status& status();
  const Status& status() const;

  const infrastructure::Path& home_directory() const;
  const std::vector<infrastructure::Path>& edge_path() const {
    return edge_path_;
  }

  // Returns the subdirectory of `edge_path` where dynamic state about files
  // edited (e.g., backup of unsaved modifications, log of interactions, etc.)
  // should be kept.
  static infrastructure::PathComponent StatePathComponent();

  language::gc::Pool& gc_pool() { return gc_pool_; }

  language::gc::Root<vm::Environment> environment() { return environment_; }

  infrastructure::Path expand_path(infrastructure::Path path) const;

  void PushSignal(infrastructure::UnixSignal signal);
  void ProcessSignals();
  void StartHandlingInterrupts() { handling_interrupts_ = true; }
  bool handling_interrupts() const { return handling_interrupts_; }
  bool handling_stop_signals() const;

  void ExecutionIteration(infrastructure::execution::IterationHandler& handler);

  InsertHistory& insert_history() { return insert_history_; }

  infrastructure::audio::Player& audio_player() const { return audio_player_; }

  std::optional<language::gc::Root<InputReceiver>> keyboard_redirect() const;
  // Returns the old value.
  std::optional<language::gc::Root<InputReceiver>> set_keyboard_redirect(
      std::optional<language::gc::Root<InputReceiver>> keyboard_redirect);

  // Executes pending work from all buffers.
  void ExecutePendingWork();
  std::optional<struct timespec> WorkQueueNextExecution() const;
  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>>& work_queue()
      const;
  concurrent::ThreadPoolWithWorkQueue& thread_pool();

 private:
  futures::Value<language::EmptyValue> ProcessInput(
      std::shared_ptr<std::vector<infrastructure::ExtendedChar>> input,
      size_t start_index);

  static void NotifyInternalEvent(SharedData& data);

  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;
  language::NonNull<std::shared_ptr<concurrent::ThreadPoolWithWorkQueue>>
      thread_pool_;

  language::gc::Pool gc_pool_;

  EdgeStructInstance<std::wstring> string_variables_;
  EdgeStructInstance<bool> bool_variables_;
  EdgeStructInstance<int> int_variables_;
  EdgeStructInstance<double> double_variables_;

  std::map<BufferName, afc::language::gc::Root<OpenBuffer>> buffers_;
  std::optional<int> exit_value_;
  std::set<BufferName> dirty_buffers_saved_to_backup_;

  const std::vector<infrastructure::Path> edge_path_;

  const language::gc::Root<vm::Environment> environment_;

  // Should only be directly used when the editor has no buffer.
  const language::gc::Root<MapModeCommands> default_commands_;
  std::optional<language::gc::Root<InputReceiver>> keyboard_redirect_;

  // Used to honor command line argument frames_per_second. Holds the earliest
  // time when a redraw should be allowed.
  struct timespec next_screen_update_ = {0, 0};
  ScreenState screen_state_;

  // Initially we don't consume SIGINT: we let it crash the process (in case
  // the user has accidentally ran Edge). However, as soon as the user starts
  // actually using Edge (e.g. modifies a buffer), we start consuming it.
  bool handling_interrupts_ = false;

  std::vector<infrastructure::UnixSignal> pending_signals_;

  Modifiers modifiers_;
  LineMarks line_marks_;

  infrastructure::audio::Player& audio_player_;

  InsertHistory insert_history_;

  BuffersList buffer_tree_;

  const language::gc::Root<BufferRegistry> buffer_registry_;
};

}  // namespace afc::editor

#endif
