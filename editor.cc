#include <memory>
#include <list>
#include <string>

#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

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
