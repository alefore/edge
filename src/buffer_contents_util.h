#ifndef __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__

#include <memory>
#include <string>

#include "src/buffer_contents.h"
#include "src/line_column.h"

namespace afc::editor {
struct CurrentTokenOptions {
  const BufferContents& contents;
  LineColumn line_column;
  std::wstring token_characters;
};
std::wstring GetCurrentToken(CurrentTokenOptions options);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
