#ifndef __AFC_EDITOR_EDITOR_H__
#define __AFC_EDITOR_EDITOR_H__

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "command_mode.h"
#include "lazy_string.h"
#include "memory_mapped_file.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::min;
using std::max;

struct Line {
  size_t size() const { return contents->size(); }
  shared_ptr<LazyString> contents;
};

struct OpenBuffer {
  OpenBuffer(unique_ptr<MemoryMappedFile> input);

  void CheckPosition();
  shared_ptr<Line> current_line() const {
    return contents.at(current_position_line);
  }

  vector<shared_ptr<Line>> contents;

  int view_start_line;
  size_t current_position_line;
  size_t current_position_col;
};

struct EditorState {
  EditorState()
      : current_buffer(0),
        terminate(false),
        repetitions(1),
        mode(std::move(NewCommandMode())) {}

  void CheckPosition() {
    buffers[current_buffer]->CheckPosition();
  }

  vector<shared_ptr<OpenBuffer>> buffers;
  int current_buffer;
  bool terminate;

  int repetitions;
  unique_ptr<EditorMode> mode;
};

}  // namespace editor
}  // namespace afc

#endif
