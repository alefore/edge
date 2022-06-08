#include "src/tokenize.h"

#include <glog/logging.h>

#include "src/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::editor {
using ::operator<<;

using language::NonNull;

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
        if (command.get(i) == L'\\') {
          ++i;
        }
        if (i.ToDelta() < command.size()) {
          token.value.push_back(command.get(i));
          ++i;
        }
      }
    } else if (c == '\\') {
      ++i;
      if (i.ToDelta() < command.size()) {
        token.value.push_back(command.get(i));
      }
    } else {
      token.value.push_back(c);
    }
  }
  push(ColumnNumber() + command.size());
  return output;
}

void PushIfNonEmpty(const NonNull<std::shared_ptr<LazyString>>& source,
                    Token token, std::vector<Token>& output) {
  CHECK_LE(token.begin, token.end);
  if (token.begin < token.end) {
    token.value =
        Substring(source, token.begin, token.end - token.begin)->ToString();
    output.push_back(std::move(token));
  }
}

std::vector<Token> TokenizeGroupsAlnum(
    const NonNull<std::shared_ptr<LazyString>>& name) {
  std::vector<Token> output;
  for (ColumnNumber i; i.ToDelta() < name->size();) {
    while (i.ToDelta() < name->size() && !isalnum(name->get(i))) {
      // if (name->get(i) == L'\\') ++i;
      ++i;
    }
    Token token;
    token.begin = i;
    while (i.ToDelta() < name->size() && isalnum(name->get(i))) {
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

std::vector<Token> TokenizeNameForPrefixSearches(
    const NonNull<std::shared_ptr<LazyString>>& name) {
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

std::vector<Token> ExtendTokensToEndOfString(
    NonNull<std::shared_ptr<LazyString>> str, std::vector<Token> tokens) {
  std::vector<Token> output;
  output.reserve(tokens.size());
  for (auto& token : tokens) {
    output.push_back(Token{.value = Substring(str, token.begin)->ToString(),
                           .begin = token.begin,
                           .end = ColumnNumber() + str->size()});
  }
  return output;
}

std::optional<std::vector<Token>> FindFilterPositions(
    const std::vector<Token>& filter, std::vector<Token> substrings) {
  std::vector<Token> output;
  for (auto& filter_token : filter) {
    if (auto token = FindPrefixInTokens(filter_token.value, substrings);
        token.has_value()) {
      output.push_back(std::move(token.value()));
    } else {
      VLOG(8) << "Token not found: " << filter_token.value;
      return std::nullopt;
    }
  }
  return output;
}

}  // namespace afc::editor
