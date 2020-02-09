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

// Simpler version of `TokenizeBySpaces` that ignores quotes.
std::vector<Token> TokenizeBySpacesSimple(
    const std::shared_ptr<LazyString>& name);

// Given: src/CreateSomethingOrOther/buffer_list.cc
// Returns: "src", "Create", "Something", "Or", "Other", "buffer", "list", "cc"
std::vector<Token> TokenizeNameForPrefixSearches(
    const std::shared_ptr<LazyString>& path);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TOKENIZE_H__
