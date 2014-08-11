#include "buffer.h"

#include <memory>
#include <list>
#include <string>

#include "editor.h"
#include "substring.h"

namespace {

static void NoopLoader(afc::editor::OpenBuffer* output) {}

}  // namespace

namespace afc {
namespace editor {

OpenBuffer::OpenBuffer()
    : view_start_line(0),
      current_position_line(0),
      current_position_col(0),
      saveable(false),
      loader(NoopLoader) {}

void OpenBuffer::Reload() {
  contents.clear();
  view_start_line = 0;
  current_position_line = 0;
  current_position_col = 0;
  saveable = false;
  loader(this);
}

void OpenBuffer::AppendLazyString(shared_ptr<LazyString> input) {
  size_t size = input->size();
  size_t start = 0;
  for (size_t i = 0; i < size; i++) {
    if (input->get(i) == '\n') {
      shared_ptr<Line> line(new Line());
      line->contents = Substring(input, start, i - start);
      contents.push_back(line);
      start = i + 1;
    }
  }
}

void OpenBuffer::MaybeAdjustPositionCol() {
  size_t line_length = current_line()->contents->size();
  if (current_position_col > line_length) {
    current_position_col = line_length;
  }
}

void OpenBuffer::CheckPosition() {
  if (current_position_line >= contents.size()) {
    current_position_line = contents.size();
    if (current_position_line > 0) {
      current_position_line--;
    }
  }
}

}  // namespace editor
}  // namespace afc
