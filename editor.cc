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
    : current_buffer(buffers.end()),
      terminate(false),
      direction(FORWARDS),
      repetitions(1),
      structure(CHAR),
      default_structure(CHAR),
      mode(std::move(NewCommandMode())),
      visible_lines(1),
      screen_needs_redraw(false),
      status_prompt(false),
      status(""),
      home_directory(GetHomeDirectory()),
      edge_path(GetEdgeConfigPath(home_directory)) {}

void EditorState::SetStructure(Structure new_structure) {
  default_structure = EditorState::CHAR;
  structure = new_structure;
}

void EditorState::SetDefaultStructure(Structure new_structure) {
  default_structure = new_structure;
  ResetStructure();
}

void EditorState::MoveBufferForwards(size_t times) {
  PushCurrentPosition();
  if (current_buffer == buffers.end()) {
    if (buffers.empty()) { return; }
    current_buffer = buffers.begin();
  }
  times = times % buffers.size();
  for (size_t i = 0; i < times; i++) {
    current_buffer++;
    if (current_buffer == buffers.end()) {
      current_buffer = buffers.begin();
    }
  }
  current_buffer->second->Enter(this);
}

void EditorState::MoveBufferBackwards(size_t times) {
  PushCurrentPosition();
  if (current_buffer == buffers.end()) {
    if (buffers.empty()) { return; }
    current_buffer = buffers.end();
    current_buffer--;
  }
  times = times % buffers.size();
  for (size_t i = 0; i < times; i++) {
    if (current_buffer == buffers.begin()) {
      current_buffer = buffers.end();
    }
    current_buffer--;
  }
  current_buffer->second->Enter(this);
}

void EditorState::PushCurrentPosition() {
  if (current_buffer == buffers.end()) { return; }
  positions_stack.emplace_back();
  Position& position = positions_stack.back();
  position.buffer = current_buffer->first;
  position.line = current_buffer->second->current_position_line();
  position.col = current_buffer->second->current_position_col();
}

void EditorState::PopLastNearPositions() {
  while (true) {
    if (positions_stack.empty() || current_buffer == buffers.end()) {
      return;
    }
    const auto& pos = positions_stack.back();
    const auto& buffer = get_current_buffer();
    if (pos.buffer != current_buffer->first
        || pos.line != buffer->current_position_line()
        || pos.col != buffer->current_position_col()) {
      return;
    }
    positions_stack.pop_back();
  }
}

}  // namespace editor
}  // namespace afc
