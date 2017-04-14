#include "buffer_contents.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "wstring.h"

namespace afc {
namespace editor {

wstring BufferContents::ToString() const {
  wstring output;
  output.reserve(CountCharacters());
  ForEach([&output](size_t position, const Line& line) {
            output.append((position == 0 ? L"" : L"\n") + line.ToString());
            return true;
          });
  return output;
}

void BufferContents::insert(size_t position, const BufferContents& source,
                            size_t first_line, size_t last_line) {
  CHECK_LT(position, size());
  CHECK_LT(first_line, source.size());
  CHECK_LE(first_line, last_line);
  CHECK_LE(last_line, source.size());
  lines_.insert(lines_.begin() + position, source.lines_.begin() + first_line,
                source.lines_.begin() + last_line);
}

bool BufferContents::ForEach(
    const std::function<bool(size_t, const Line&)>& callback) const {
  size_t position = 0;
  for (const auto& line : lines_) {
    if (!callback(position++, *line)) { return false; }
  }
  return true;
}

void BufferContents::ForEach(
    const std::function<void(const Line&)>& callback) const {
  ForEach([callback](size_t, const Line& line) {
            callback(line);
            return true;
          });
}

void BufferContents::ForEach(const std::function<void(wstring)>& callback)
    const {
  ForEach([callback](const Line& line) { callback(line.ToString()); });
}

size_t BufferContents::CountCharacters() const {
  size_t output = 0;
  ForEach([&output](const Line& line) {
    output += line.size() + 1;  // \n.
  });
  if (output > 0) {
    output--;  // Last line has no \n.
  }
  return output;
}

}  // namespace editor
}  // namespace afc