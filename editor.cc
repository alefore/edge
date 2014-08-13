#include <memory>
#include <list>
#include <string>

#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

void EditorState::MoveBufferForwards(size_t times) {
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
}

void EditorState::MoveBufferBackwards(size_t times) {
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
}

}  // namespace editor
}  // namespace afc
