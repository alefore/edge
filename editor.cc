#include <iostream>
#include <memory>
#include <list>
#include <string>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
}

#include "editor.h"
#include "char_buffer.h"
#include "substring.h"

namespace {

using std::make_pair;
using std::string;
using std::stringstream;
using std::to_string;
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
      default_direction_(FORWARDS),
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

EditorState::~EditorState() {
  // TODO: Replace this with a custom deleter in the shared_ptr.  Simplify
  // CloseBuffer accordingly.
  for (auto& buffer : buffers_) {
    buffer.second->Close(this);
  }
}

void EditorState::CloseBuffer(
    map<string, shared_ptr<OpenBuffer>>::iterator buffer) {
  ScheduleRedraw();
  if (current_buffer_ == buffer) {
    if (buffers_.size() == 1) {
      current_buffer_ = buffers_.end();
    } else {
      current_buffer_ = buffer == buffers_.begin() ? buffers_.end() : buffer;
      current_buffer_--;
    }

    if (current_buffer_ != buffers_.end()) {
      current_buffer_->second->Enter(this);
    }
  }

  buffer->second->Close(this);
  buffers_.erase(buffer);
  assert(current_buffer_ != buffers_.end());
}

void EditorState::set_direction(Direction direction) {
  direction_ = direction;
}

void EditorState::set_default_direction(Direction direction) {
  default_direction_ = direction;
  ResetDirection();
}

void EditorState::set_structure(Structure structure) {
  structure_ = structure;
}

void EditorState::set_default_structure(Structure structure) {
  default_structure_ = structure;
  ResetStructure();
}

void EditorState::MoveBufferForwards(size_t times) {
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
  PushCurrentPosition();
}

void EditorState::MoveBufferBackwards(size_t times) {
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
  PushCurrentPosition();
}

// We will store the positions in a special buffer.  They will be sorted from
// old (top) to new (bottom), one per line.  Each line will be of the form:
//
//   line column buffer
//
// The current line position is set to one line after the line to be returned
// by a pop.  To insert a new position, we insert it right at the current line.

static const char* kPositionsBufferName = "- positions";

void EditorState::PushCurrentPosition() {
  if (!has_current_buffer()) { return; }
  auto it = buffers_.find(kPositionsBufferName);
  if (it == buffers_.end()) {
    it = buffers_.insert(
        make_pair(kPositionsBufferName,
                  new OpenBuffer(this, kPositionsBufferName)))
        .first;
  }
  assert(it->second != nullptr);
  assert(!it->second->contents()->empty());
  assert(it->second->position().line < it->second->contents()->size());
  shared_ptr<Line> line(new Line());
  line->contents = NewCopyString(
      current_buffer_->second->position().ToString()
      + " " + current_buffer_->first);
  it->second->contents()->insert(
      it->second->contents()->begin()
      + it->second->current_position_line(),
      line);
  it->second->set_current_position_line(
      it->second->current_position_line() + 1);
  assert(it->second->position().line < it->second->contents()->size());
  if (it == current_buffer_) {
    ScheduleRedraw();
  }
}

static BufferPosition PositionFromLine(const string& line) {
  stringstream line_stream(line);
  BufferPosition pos;
  line_stream >> pos.position.line >> pos.position.column;
  line_stream.get();
  getline(line_stream, pos.buffer);
  return pos;
}

bool EditorState::HasPositionsInStack() {
  auto it = buffers_.find(kPositionsBufferName);
  return it != buffers_.end() && it->second->contents()->size() > 1;
}

BufferPosition EditorState::ReadPositionsStack() {
  assert(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  return PositionFromLine(buffer->current_line()->contents->ToString());
}

bool EditorState::MovePositionsStack(Direction direction) {
  // The directions here are somewhat counterintuitive: FORWARDS means the user
  // is actually going "back" in the history, which means we have to decrement
  // the line counter.
  assert(HasPositionsInStack());
  auto buffer = buffers_.find(kPositionsBufferName)->second;
  if (direction == BACKWARDS) {
    if (buffer->current_position_line() + 1 >= buffer->contents()->size()) {
      return false;
    }
    buffer->set_current_position_line(buffer->current_position_line() + 1);
    return true;
  }

  if (buffer->current_position_line() == 0) {
    return false;
  }
  buffer->set_current_position_line(buffer->current_position_line() - 1);
  return true;
}

void EditorState::ApplyToCurrentBuffer(const Transformation& transformation) {
  assert(has_current_buffer());
  current_buffer_->second->Apply(this, transformation);
}

}  // namespace editor
}  // namespace afc
