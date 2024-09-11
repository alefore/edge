#ifndef __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
struct CurrentTokenOptions {
  const language::text::LineSequence& contents;
  language::text::LineColumn line_column;
  std::unordered_set<wchar_t> token_characters;
};
language::lazy_string::LazyString GetCurrentToken(CurrentTokenOptions options);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_CONTENTS_UTIL_H__
