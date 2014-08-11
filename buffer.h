#ifndef __AFC_EDITOR_BUFFER_H__
#define __AFC_EDITOR_BUFFER_H__

#include <map>
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
using std::map;
using std::max;
using std::min;

struct Line {
  size_t size() const { return contents->size(); }

  unique_ptr<EditorMode> activate;
  shared_ptr<LazyString> contents;
};

struct OpenBuffer {
  OpenBuffer();

  void Reload();
  void AppendLazyString(shared_ptr<LazyString> input);

  // Checks that current_position_col is in the expected range (between 0 and
  // the length of the current line).
  void MaybeAdjustPositionCol();

  void CheckPosition();
  shared_ptr<Line> current_line() const {
    return contents.at(current_position_line);
  }

  vector<shared_ptr<Line>> contents;

  int view_start_line;
  size_t current_position_line;
  size_t current_position_col;

  bool saveable;
  function<void(OpenBuffer*)> loader;
};

}  // namespace editor
}  // namespace afc

#endif
