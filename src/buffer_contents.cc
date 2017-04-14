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
  size_t size = 0;
  ForEach(
      [&size](size_t, const Line& line) {
        size += line.size() + 1;
        return true;
      });
  size -= 1;  // Last line doesn't have EOL.

  wstring output;
  output.reserve(size);
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

}  // namespace editor
}  // namespace afc