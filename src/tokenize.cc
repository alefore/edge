#include "src/tokenize.h"

#include "src/substring.h"

namespace afc::editor {
std::vector<Token> TokenizeBySpaces(const LazyString& command) {
  std::vector<Token> output;
  Token token;
  auto push = [&](ColumnNumber end) {
    if (!token.value.empty()) {
      token.end = end;
      output.push_back(std::move(token));
    }
    token.value = L"";
    token.begin = ++end;
  };

  for (ColumnNumber i; i.ToDelta() < command.size(); ++i) {
    char c = command.get(i);
    if (c == ' ') {
      push(i);
    } else if (c == '\"') {
      ++i;
      while (i.ToDelta() < command.size() && command.get(i) != '\"') {
        token.value.push_back(command.get(i));
        ++i;
      }
    } else {
      token.value.push_back(c);
    }
  }
  push(ColumnNumber() + command.size());
  return output;
}

void PushIfNonEmpty(const std::shared_ptr<LazyString>& source, Token token,
                    std::vector<Token>* output) {
  CHECK_LE(token.begin, token.end);
  CHECK(output != nullptr);
  if (token.begin < token.end) {
    token.value =
        Substring(source, token.begin, token.end - token.begin)->ToString();
    output->push_back(std::move(token));
  }
}

std::vector<Token> TokenizeGroupsAlnum(
    const std::shared_ptr<LazyString>& name) {
  std::vector<Token> output;
  for (ColumnNumber i; i.ToDelta() < name->size(); ++i) {
    while (i.ToDelta() < name->size() && !isalnum(name->get(i))) {
      ++i;
    }
    Token token;
    token.begin = i;
    while (i.ToDelta() < name->size() && isalnum(name->get(i))) {
      ++i;
    }
    token.end = i;
    PushIfNonEmpty(name, std::move(token), &output);
  }
  return output;
}

std::vector<Token> TokenizeNameForPrefixSearches(
    const std::shared_ptr<LazyString>& name) {
  std::vector<Token> output;
  for (const auto& input_token : TokenizeGroupsAlnum(name)) {
    ColumnNumber i = input_token.begin;
    while (i < input_token.end) {
      Token output_token;
      output_token.begin = i;
      ++i;
      while (i < input_token.end &&
             ((isupper(name->get(i - ColumnNumberDelta(1))) ||
               islower(name->get(i))))) {
        ++i;
      }
      output_token.end = i;
      PushIfNonEmpty(name, std::move(output_token), &output);
    }
  }
  return output;
}

namespace {
// Does any of the elements in `name_tokens` start with `prefix`? If so, returns
// a corresponding token.
std::optional<Token> FindPrefixInTokens(std::wstring prefix,
                                        std::vector<Token> name_tokens) {
  for (auto& name_token : name_tokens) {
    if (name_token.value.size() >= prefix.size() &&
        std::equal(
            prefix.begin(), prefix.end(), name_token.value.begin(),
            [](wchar_t a, wchar_t b) { return tolower(a) == tolower(b); })) {
      return Token{.value = name_token.value.substr(0, prefix.size()),
                   .begin = name_token.begin,
                   .end = name_token.begin + ColumnNumberDelta(prefix.size())};
    }
  }
  return std::nullopt;
}
}  // namespace

std::vector<Token> ExtendTokensToEndOfString(std::shared_ptr<LazyString> str,
                                             std::vector<Token> tokens) {
  std::vector<Token> output;
  output.reserve(tokens.size());
  for (auto& token : tokens) {
    output.push_back(Token{.value = Substring(str, token.begin)->ToString(),
                           .begin = token.begin,
                           .end = ColumnNumber() + str->size()});
  }
  return output;
}

std::vector<Token> FindFilterPositions(const std::vector<Token>& filter,
                                       std::vector<Token> substrings) {
  std::vector<Token> output;
  for (auto& filter_token : filter) {
    if (auto token = FindPrefixInTokens(filter_token.value, substrings);
        token.has_value()) {
      output.push_back(std::move(token.value()));
    } else {
      return {};
    }
  }
  return output;
}

}  // namespace afc::editor
