#include <memory>
#include <list>
#include <string>

extern "C" {
#include <sys/types.h>
#include <pwd.h>
}

#include "editor.h"
#include "substring.h"

namespace {

using std::string;
using std::vector;

static string GetHomeDirectory() {
  char* env = getenv("HOME");
  if (env != nullptr) { return env; }
  struct passwd* entry = getpwuid(getuid());
  if (entry != nullptr) { return entry->pw_dir; }
  return "/";  // What else?
}

static vector<string> GetEdgeConfigPath(const string& home) {
  vector<string> output;
  char* env = getenv("EDGE_PATH");
  if (env != nullptr) {
    // TODO: Handle multiple directories separated with colons.
    // TODO: stat it and don't add it if it doesn't exist.
    output.push_back(env);
  }
  // TODO: Don't add it if it doesn't exist or it's already there.
  output.push_back(home + "/.edge");
  return output;
}

}  // namespace

namespace afc {
namespace editor {

EditorState::EditorState()
    : current_buffer_(buffers_.end()),
      terminate_(false),
      direction_(FORWARDS),
      repetitions_(1),
      structure_(CHAR),
      default_structure_(CHAR),
      mode_(std::move(NewCommandMode())),
      visible_lines_(1),
      screen_needs_redraw_(false),
      status_prompt_(false),
      status_(""),
      home_directory_(GetHomeDirectory()),
      edge_path_(GetEdgeConfigPath(home_directory_)) {}

void EditorState::set_structure(Structure structure) {
  default_structure_ = EditorState::CHAR;
  structure_ = structure;
}

void EditorState::set_default_structure(Structure structure) {
  default_structure_ = structure;
  ResetStructure();
}

void EditorState::MoveBufferForwards(size_t times) {
  PushCurrentPosition();
  if (current_buffer_ == buffers_.end()) {
    if (buffers_.empty()) { return; }
    current_buffer_ = buffers_.begin();
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    current_buffer_++;
    if (current_buffer_ == buffers_.end()) {
      current_buffer_ = buffers_.begin();
    }
  }
  current_buffer_->second->Enter(this);
}

void EditorState::MoveBufferBackwards(size_t times) {
  PushCurrentPosition();
  if (current_buffer_ == buffers_.end()) {
    if (buffers_.empty()) { return; }
    current_buffer_ = buffers_.end();
    current_buffer_--;
  }
  times = times % buffers_.size();
  for (size_t i = 0; i < times; i++) {
    if (current_buffer_ == buffers_.begin()) {
      current_buffer_ = buffers_.end();
    }
    current_buffer_--;
  }
  current_buffer_->second->Enter(this);
}

void EditorState::PushCurrentPosition() {
  if (!has_current_buffer()) { return; }
  positions_stack_.emplace_back();
  Position& position = positions_stack_.back();
  position.buffer = current_buffer_->first;
  position.line = current_buffer_->second->current_position_line();
  position.col = current_buffer_->second->current_position_col();
}

void EditorState::PopLastNearPositions() {
  while (true) {
    if (positions_stack_.empty() || has_current_buffer()) {
      return;
    }
    const auto& pos = positions_stack_.back();
    const auto& buffer = current_buffer()->second;
    if (pos.buffer != current_buffer_->first
        || pos.line != buffer->current_position_line()
        || pos.col != buffer->current_position_col()) {
      return;
    }
    positions_stack_.pop_back();
  }
}

}  // namespace editor
}  // namespace afc
