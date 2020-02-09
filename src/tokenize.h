#ifndef __AFC_EDITOR_TOKENIZE_H__
#define __AFC_EDITOR_TOKENIZE_H__

#include <wchar.h>

#include <vector>

#include "src/lazy_string.h"
#include "src/line_column.h"

namespace afc::editor {
struct Token {
  std::wstring value;
  ColumnNumber begin;
  // `end` is the first column that isn't part of the token.
  ColumnNumber end;
};

// Given a string containing: foo    bar "hey there"
// Returns: {"foo", "bar", "hey there"}
std::vector<Token> TokenizeBySpaces(const LazyString& command);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TOKENIZE_H__
