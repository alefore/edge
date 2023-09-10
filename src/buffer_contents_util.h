#ifndef __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__

#include <memory>
#include <string>

#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
struct CurrentTokenOptions {
  const language::text::LineSequence& contents;
  language::text::LineColumn line_column;
  std::wstring token_characters;
};
std::wstring GetCurrentToken(CurrentTokenOptions options);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
