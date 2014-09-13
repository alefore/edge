#ifndef __AFC_EDITOR_EDITOR_H__
#define __AFC_EDITOR_EDITOR_H__

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "buffer.h"
#include "command_mode.h"
#include "direction.h"
#include "lazy_string.h"
#include "memory_mapped_file.h"
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

struct BufferPosition {
  string buffer;
  LineColumn position;
};

class EditorState {
 public:
  enum Structure {
    CHAR,
    WORD,
    LINE,
    PAGE,
    SEARCH,
    BUFFER,
  };

  static Structure LowerStructure(Structure s) {
    switch (s) {
      case CHAR: return CHAR;
      case WORD: return CHAR;
      case LINE: return WORD;
      case PAGE: return LINE;
      case SEARCH: return PAGE;
      case BUFFER: return SEARCH;
    }
    assert(false);
  }

  EditorState();
  ~EditorState();

  void CheckPosition() {
    if (has_current_buffer()) {
      current_buffer_->second->CheckPosition();
    }
  }

  void CloseBuffer(const map<string, shared_ptr<OpenBuffer>>::iterator buffer);

  map<string, shared_ptr<OpenBuffer>>* buffers() {
    return &buffers_;
  }
  void set_current_buffer(map<string, shared_ptr<OpenBuffer>>::iterator it) {
    current_buffer_ = it;
  }
  bool has_current_buffer() const {
    return current_buffer_ != buffers_.end();
  }
  map<string, shared_ptr<OpenBuffer>>::iterator current_buffer() {
    return current_buffer_;
  }
  map<string, shared_ptr<OpenBuffer>>::const_iterator current_buffer() const {
    return current_buffer_;
  }
  bool terminate() const { return terminate_; }
  void set_terminate(bool value) { terminate_ = value; }

  Direction direction() const { return direction_; }
  void set_direction(Direction direction);
  void ResetDirection() { direction_ = default_direction_; }
  Direction default_direction() const { return default_direction_; }
  void set_default_direction(Direction direction);

  size_t repetitions() const { return repetitions_; }
  void ResetRepetitions() { repetitions_ = 1; }
  void set_repetitions(size_t value) { repetitions_ = value; }

  // TODO: Maybe use a compiled regexp?
  const string& last_search_query() const { return last_search_query_; }
  void set_last_search_query(const string& query) {
    last_search_query_ = query;
  }

  Structure structure() const { return structure_; }
  void set_structure(Structure structure);
  void ResetStructure() {
    if (!sticky_structure_) {
      structure_ = Structure::CHAR;
    }
  }
  bool sticky_structure() const { return sticky_structure_; }
  void set_sticky_structure(bool sticky_structure) {
    sticky_structure_ = sticky_structure;
  }

  void ProcessInputString(const string& input) {
    for (size_t i = 0; i < input.size(); ++i) {
      ProcessInput(input[i]);
    }
  }

  void ProcessInput(int c) {
    mode()->ProcessInput(c, this);
  }

  EditorMode* mode() const { return mode_.get(); }
  void ResetMode() {
    mode_ = NewCommandMode();
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

  void PushCurrentPosition();
  bool HasPositionsInStack();
  BufferPosition ReadPositionsStack();
  bool MovePositionsStack(Direction direction);

  void set_status_prompt(bool value) { status_prompt_ = value; }
  bool status_prompt() const { return status_prompt_; }
  void SetStatus(const string& status);
  void ResetStatus() { SetStatus(""); }
  const string& status() const { return status_; }

  const string& home_directory() const { return home_directory_; }
  const vector<string>& edge_path() const { return edge_path_; }

  void ApplyToCurrentBuffer(const Transformation& transformation);

  Environment* environment() { return &environment_; }

  // Meant to be used to construct afc::vm::Evaluator::ErrorHandler instances.
  void DefaultErrorHandler(const string& error_description);

  string expand_path(const string& path);

 private:
  map<string, shared_ptr<OpenBuffer>> buffers_;
  map<string, shared_ptr<OpenBuffer>>::iterator current_buffer_;
  bool terminate_;

  Direction direction_;
  Direction default_direction_;
  size_t repetitions_;
  string last_search_query_;
  Structure structure_;
  bool sticky_structure_;

  unique_ptr<EditorMode> mode_;

  // Set by the terminal handler.
  size_t visible_lines_;

  bool screen_needs_redraw_;

  bool status_prompt_;
  string status_;

  string home_directory_;
  vector<string> edge_path_;

  Environment environment_;
};

}  // namespace editor
}  // namespace afc

#endif
