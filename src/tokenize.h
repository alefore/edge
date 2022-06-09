#ifndef __AFC_EDITOR_TOKENIZE_H__
#define __AFC_EDITOR_TOKENIZE_H__

#include <wchar.h>

#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/line_column.h"

namespace afc::editor {
struct Token {
  std::wstring value = L"";
  language::lazy_string::ColumnNumber begin;
  // `end` is the first column that isn't part of the token.
  language::lazy_string::ColumnNumber end;
};

bool operator==(const Token& a, const Token& b);
std::ostream& operator<<(std::ostream& os, const Token& lc);

// Given the string: "foo    bar \"hey there\""
// Returns: {"foo", "bar", "hey there"}
//
// Can handle \. For example, the string `foo\" bar\\x` gives the two tokens
// `foo"` and `bar\x`.
std::vector<Token> TokenizeBySpaces(
    const language::lazy_string::LazyString& command);

// Given: src/CreateSomethingOrOther/buffer_list.cc
// Returns: "src", "Create", "Something", "Or", "Other", "buffer", "list", "cc"
std::vector<Token> TokenizeNameForPrefixSearches(
    const language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>&
        path);

// Given a string "foo bar hey" and the tokens "foo", "bar", and "hey", returns
// the tokens for "foo bar hey", "bar hey", "hey". This is useful to turn the
// output of `TokenizeNameForPrefixSearches` into a form that's useful to feed
// to `AllFilterTokensAreValidPrefixes`, allowing filter tokens to extend past
// a given element from `tokens` (.e.g., searching for "foo ba" will match).
std::vector<Token> ExtendTokensToEndOfString(
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
    std::vector<Token> tokens);

// If all tokens in `filter` are valid prefix (by a case-insensitive comparison)
// of a token in `substrings`, returns a vector with the same length as
// `filter`, containing one token for the first match of each filter. Otherwise,
// returns std::nullopt.
std::optional<std::vector<Token>> FindFilterPositions(
    const std::vector<Token>& filter, std::vector<Token> substrings);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TOKENIZE_H__
