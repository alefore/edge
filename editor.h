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

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::list;
using std::map;
using std::max;
using std::min;

struct Position {
  string buffer;
  size_t line;
  size_t col;
};

struct EditorState {
  enum Structure {
    CHAR,
    LINE,
    BUFFER,
  };

  static Structure LowerStructure(Structure s) {
    switch (s) {
      case CHAR: return CHAR;
      case LINE: return CHAR;
      case BUFFER: return LINE;
    }
    assert(false);
  }

  EditorState();

  void CheckPosition() {
    get_current_buffer()->CheckPosition();
  }

  shared_ptr<OpenBuffer> get_current_buffer() const {
    return current_buffer->second;
  }

  void SetStructure(Structure structure);
  void SetDefaultStructure(Structure structure);
  void ResetStructure() { structure = default_structure; }

  void MoveBufferForwards(size_t times);
  void MoveBufferBackwards(size_t times);

  void PushCurrentPosition();
  void PopLastNearPositions();

  map<string, shared_ptr<OpenBuffer>> buffers;
  map<string, shared_ptr<OpenBuffer>>::iterator current_buffer;
  bool terminate;

  Direction direction;
  size_t repetitions;
  Structure structure;
  Structure default_structure;

  unique_ptr<EditorMode> mode;

  // Set by the terminal handler.
  size_t visible_lines;

  bool screen_needs_redraw;

  list<Position> positions_stack;

  bool status_prompt;
  string status;

  string home_directory;
  vector<string> edge_path;
};

}  // namespace editor
}  // namespace afc

#endif
