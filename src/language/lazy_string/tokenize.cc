#include "src/language/lazy_string/tokenize.h"

#include <glog/logging.h>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
using ::operator<<;

using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;

bool operator==(const Token& a, const Token& b) {
  return a.begin == b.begin && a.end == b.end && a.value == b.value;
}

std::ostream& operator<<(std::ostream& os, const Token& t) {
  os << "[token: begin:" << t.begin << ", end: " << t.end
     << ", value: " << t.value << "]";
  return os;
}

std::vector<Token> TokenizeBySpaces(const LazyString& command) {
  std::vector<Token> output;
  Token token;
  auto push = [&](ColumnNumber end) {
    if (!token.value.IsEmpty()) {
      token.end = end;
      output.push_back(std::move(token));
    }
    token.value = LazyString{};
    token.begin = ++end;
    token.has_quotes = false;
  };

  for (ColumnNumber i; i.ToDelta() < command.size(); ++i) {
    char c = command.get(i);
    if (c == ' ') {
      push(i);
    } else if (c == '\"') {
      ++i;
      token.has_quotes = true;
      while (i.ToDelta() < command.size() && command.get(i) != '\"') {
        if (command.get(i) == L'\\') {
          ++i;
        }
        if (i.ToDelta() < command.size()) {
          token.value += LazyString{std::wstring{command.get(i)}};
          ++i;
        }
      }
    } else if (c == '\\') {
      ++i;
      token.has_quotes = true;
      if (i.ToDelta() < command.size())
        token.value += LazyString{std::wstring{command.get(i)}};
    } else {
      token.value += LazyString{std::wstring{c}};
    }
  }
  push(ColumnNumber() + command.size());
  return output;
}

void PushIfNonEmpty(const LazyString& source, Token token,
                    std::vector<Token>& output) {
  CHECK_LE(token.begin, token.end);
  if (token.begin < token.end) {
    token.value = source.Substring(token.begin, token.end - token.begin);
    output.push_back(std::move(token));
  }
}

std::vector<Token> TokenizeGroupsAlnum(const LazyString& name) {
  std::vector<Token> output;
  for (ColumnNumber i; i.ToDelta() < name.size();) {
    while (i.ToDelta() < name.size() && !isalnum(name.get(i))) {
      // if (name.get(i) == L'\\') ++i;
      ++i;
    }
    Token token;
    token.begin = i;
    while (i.ToDelta() < name.size() && isalnum(name.get(i))) {
      ++i;
    }
    token.end = i;
    VLOG(9) << "Considering token: " << token;
    PushIfNonEmpty(name, std::move(token), output);
  }
  return output;
}

#if 0
namespace {
const bool get_synthetic_features_tests_registration = tests::Register(
    L"TokenizeGroupsAlnum",
    {{.name = L"SkipsEscapeCharacters", .callback = [] {
        std::vector<Token> output =
            TokenizeGroupsAlnum(NewLazyString(L"a\\n\\n\\n\\nf"));
        CHECK_EQ(output.size(), 2ul);
        CHECK_EQ(output[0], (Token{.value = L"a",
                                   .begin = ColumnNumber(0),
                                   .end = ColumnNumber(1)}));
        CHECK_EQ(output[1], (Token{.value = L"f",
                                   .begin = ColumnNumber(9),
                                   .end = ColumnNumber(10)}));
      }}});
}
#endif

std::vector<Token> TokenizeNameForPrefixSearches(const LazyString& name) {
  std::vector<Token> output;
  for (const auto& input_token : TokenizeGroupsAlnum(name)) {
    ColumnNumber i = input_token.begin;
    while (i < input_token.end) {
      Token output_token;
      output_token.begin = i;
      ++i;
      while (i < input_token.end &&
             ((isupper(name.get(i - ColumnNumberDelta(1))) ||
               islower(name.get(i))))) {
        ++i;
      }
      output_token.end = i;
      PushIfNonEmpty(name, std::move(output_token), output);
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
    // TODO(easy, 2024-01-02): Avoid conversion to string. Stay on LazyString.
    std::wstring name_token_str = name_token.value.ToString();
    if (name_token_str.size() >= prefix.size() &&
        std::equal(
            prefix.begin(), prefix.end(), name_token_str.begin(),
            [](wchar_t a, wchar_t b) { return tolower(a) == tolower(b); })) {
      return Token{.value = name_token.value.Substring(
                       ColumnNumber(0), ColumnNumberDelta(prefix.size())),
                   .begin = name_token.begin,
                   .end = name_token.begin + ColumnNumberDelta(prefix.size())};
    }
  }
  return std::nullopt;
}
}  // namespace

std::vector<Token> ExtendTokensToEndOfString(LazyString str,
                                             std::vector<Token> tokens) {
  std::vector<Token> output;
  output.reserve(tokens.size());
  for (auto& token : tokens) {
    output.push_back(Token{.value = str.Substring(token.begin),
                           .begin = token.begin,
                           .end = ColumnNumber() + str.size()});
  }
  return output;
}

std::optional<std::vector<Token>> FindFilterPositions(
    const std::vector<Token>& filter, std::vector<Token> substrings) {
  std::vector<Token> output;
  for (auto& filter_token : filter) {
    if (auto token =
            FindPrefixInTokens(filter_token.value.ToString(), substrings);
        token.has_value()) {
      output.push_back(std::move(token.value()));
    } else {
      VLOG(8) << "Token not found: " << filter_token.value;
      return std::nullopt;
    }
  }
  return output;
}

}  // namespace afc::language::lazy_string
