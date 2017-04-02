#ifndef __AFC_EDITOR_EDITOR_H__
#define __AFC_EDITOR_EDITOR_H__

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "buffer.h"
#include "editor_mode.h"
#include "command_mode.h"
#include "direction.h"
#include "lazy_string.h"
#include "line_marks.h"
#include "memory_mapped_file.h"
#include "modifiers.h"
#include "transformation.h"
#include "vm/public/environment.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

using namespace afc::vm;

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::list;
using std::map;
using std::max;
using std::min;

class EditorState {
 public:
  EditorState();
  ~EditorState();

  void CheckPosition() {
    if (has_current_buffer()) {
      current_buffer_->second->CheckPosition();
    }
  }

  bool CloseBuffer(const map<wstring, shared_ptr<OpenBuffer>>::iterator buffer);

  const map<wstring, shared_ptr<OpenBuffer>>* buffers() const {
    return &buffers_;
  }

  map<wstring, shared_ptr<OpenBuffer>>* buffers() {
    return &buffers_;
  }

  void set_current_buffer(map<wstring, shared_ptr<OpenBuffer>>::iterator it) {
    current_buffer_ = it;
  }
  bool has_current_buffer() const {
    return current_buffer_ != buffers_.end();
  }
  map<wstring, shared_ptr<OpenBuffer>>::iterator current_buffer() {
    return current_buffer_;
  }
  map<wstring, shared_ptr<OpenBuffer>>::const_iterator current_buffer() const {
    return current_buffer_;
  }
  bool terminate() const { return terminate_; }
  bool AttemptTermination(wstring* error_description);

  void ResetModifiers() {
    ResetMode();
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

  size_t repetitions() const { return modifiers_.repetitions; }
  void ResetRepetitions() { modifiers_.ResetRepetitions(); }
  void set_repetitions(size_t value) { modifiers_.repetitions = value; }

  // TODO: Maybe use a compiled regexp?
  const wstring& last_search_query() const { return last_search_query_; }
  void set_last_search_query(const wstring& query) {
    last_search_query_ = query;
  }

  Modifiers modifiers() const { return modifiers_; }
  void set_modifiers(const Modifiers& modifiers) { modifiers_ = modifiers; }

  Structure structure() const { return modifiers_.structure; }
  void set_structure(Structure structure) { modifiers_.structure = structure; }
  void ResetStructure() { modifiers_.ResetStructure(); }

  Modifiers::StructureRange structure_range() const {
    return modifiers_.structure_range;
  }
  // TODO: Erase; it's now replaced by set_structure_range.
  void set_structure_modifier(Modifiers::StructureRange structure_range) {
    set_structure_range(structure_range);
  }
  void set_structure_range(Modifiers::StructureRange structure_range) {
    modifiers_.structure_range = structure_range;
  }

  bool sticky_structure() const { return modifiers_.sticky_structure; }
  void set_sticky_structure(bool sticky_structure) {
    modifiers_.sticky_structure = sticky_structure;
  }

  Modifiers::Insertion insertion_modifier() const {
    return modifiers_.insertion;
  }
  void set_insertion_modifier(Modifiers::Insertion insertion_modifier) {
    modifiers_.insertion = insertion_modifier;
  }
  void ResetInsertionModifier() {
    modifiers_.ResetInsertion();
  }
  Modifiers::Insertion default_insertion_modifier() const {
    return modifiers_.default_insertion;
  }
  void set_default_insertion_modifier(
      Modifiers::Insertion default_insertion_modifier) {
    modifiers_.default_insertion = default_insertion_modifier;
  }

  void ProcessInputString(const string& input) {
    for (size_t i = 0; i < input.size(); ++i) {
      ProcessInput(input[i]);
    }
  }

  void ProcessInput(int c) {
    mode()->ProcessInput(c, this);
  }

  const LineMarks* line_marks() const { return &line_marks_; }
  LineMarks* line_marks() { return &line_marks_; }

  EditorMode* mode() const { return mode_.get(); }
  std::unique_ptr<EditorMode> ResetMode() {
    auto copy = std::move(mode_);
    mode_ = default_mode_supplier_();
    return copy;
  }
  void set_mode(unique_ptr<EditorMode> mode) {
    mode_ = std::move(mode);
  }

  size_t visible_lines() const { return visible_lines_; }
  void set_visible_lines(size_t value) { visible_lines_ = value; }

  void MoveBufferForwards(size_t times);
  void MoveBufferBackwards(size_t times);

  void ScheduleRedraw() { screen_needs_redraw_ = true; }
  void set_screen_needs_redraw(bool value) { screen_needs_redraw_ = value; }
  bool screen_needs_redraw() const { return screen_needs_redraw_; }
  void set_screen_needs_hard_redraw(bool value) {
    screen_needs_hard_redraw_ = value;
  }
  bool screen_needs_hard_redraw() const { return screen_needs_hard_redraw_; }

  void PushCurrentPosition();
  void PushPosition(LineColumn position);
  bool HasPositionsInStack();
  BufferPosition ReadPositionsStack();
  bool MovePositionsStack(Direction direction);

  void set_status_prompt(bool value) { status_prompt_ = value; }
  bool status_prompt() const { return status_prompt_; }
  void set_status_prompt_column(int column) { status_prompt_column_ = column; }
  int status_prompt_column() const {
    CHECK(status_prompt_);
    return status_prompt_column_;
  }
  void SetStatus(const wstring& status);
  void ResetStatus() { SetStatus(L""); }
  const wstring& status() const { return status_; }

  const wstring& home_directory() const { return home_directory_; }
  const vector<wstring>& edge_path() const { return edge_path_; }

  void ApplyToCurrentBuffer(unique_ptr<Transformation> transformation);

  Environment* environment() { return &environment_; }

  // Meant to be used to construct afc::vm::Evaluator::ErrorHandler instances.
  void DefaultErrorHandler(const wstring& error_description);

  wstring expand_path(const wstring& path);

  void PushSignal(int signal) { pending_signals_.push_back(signal); }
  void ProcessSignals();
  void StartHandlingInterrupts() { handling_interrupts_ = true; }
  bool handling_interrupts() const { return handling_interrupts_; }

 private:
  Environment BuildEditorEnvironment();

  map<wstring, shared_ptr<OpenBuffer>> buffers_;
  map<wstring, shared_ptr<OpenBuffer>>::iterator current_buffer_;
  bool terminate_ = false;

  wstring home_directory_;
  vector<wstring> edge_path_;

  Environment environment_;

  wstring last_search_query_;

  const std::function<unique_ptr<EditorMode>()> default_mode_supplier_;
  unique_ptr<EditorMode> mode_;

  // Set by the terminal handler.
  size_t visible_lines_;

  bool screen_needs_redraw_;
  bool screen_needs_hard_redraw_;

  bool status_prompt_;
  int status_prompt_column_;
  wstring status_;

  // Initially we don't consume SIGINT: we let it crash the process (in case the
  // user has accidentally ran Edge). However, as soon as the user starts
  // actually using Edge (e.g. modifies a buffer), we start consuming it.
  bool handling_interrupts_ = false;

  vector<int> pending_signals_;

  Modifiers modifiers_;
  LineMarks line_marks_;
};

}  // namespace editor
}  // namespace afc

#endif
