#ifndef __AFC_EDITOR_EDITOR_H__
#define __AFC_EDITOR_EDITOR_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "buffer.h"
#include "command_mode.h"
#include "direction.h"
#include "lazy_string.h"
#include "memory_mapped_file.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::map;
using std::max;
using std::min;

struct EditorState {
  EditorState()
      : current_buffer(buffers.end()),
        terminate(false),
        direction(FORWARDS),
        repetitions(1),
        structure(0),
        default_structure(0),
        mode(std::move(NewCommandMode())),
        visible_lines(1),
        screen_needs_redraw(false),
        status("") {}

  void CheckPosition() {
    get_current_buffer()->CheckPosition();
  }

  shared_ptr<OpenBuffer> get_current_buffer() const {
    return current_buffer->second;
  }

  void ResetStructure() { structure = default_structure; }

  void MoveBufferForwards(size_t times);
  void MoveBufferBackwards(size_t times);

  map<string, shared_ptr<OpenBuffer>> buffers;
  map<string, shared_ptr<OpenBuffer>>::iterator current_buffer;
  bool terminate;

  Direction direction;
  size_t repetitions;
  int structure;
  int default_structure;

  unique_ptr<EditorMode> mode;

  // Set by the terminal handler.
  size_t visible_lines;

  bool screen_needs_redraw;

  string status;
};

}  // namespace editor
}  // namespace afc

#endif
