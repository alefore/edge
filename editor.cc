#include <memory>
#include <list>
#include <string>

#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

static vector<shared_ptr<Line>> ParseInput(
    shared_ptr<LazyString> input) {
  vector<shared_ptr<Line>> output;
  size_t size = input->size();
  size_t start = 0;
  for (int i = 0; i < size; i++) {
    if (input->get(i) == '\n') {
      shared_ptr<Line> line(new Line());
      line->contents = Substring(input, start, i - start);
      output.push_back(line);
      start = i + 1;
    }
  }
  return output;
}

OpenBuffer::OpenBuffer()
    : view_start_line(0),
      current_position_line(0),
      current_position_col(0) {}

OpenBuffer::OpenBuffer(unique_ptr<MemoryMappedFile> input)
    : contents(ParseInput(std::move(input))),
      view_start_line(0),
      current_position_line(0),
      current_position_col(0) {}

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
