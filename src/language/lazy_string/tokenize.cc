#include "src/language/lazy_string/tokenize.h"

#include <glog/logging.h>

#include <utility>

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;

namespace afc::language::lazy_string {
using ::operator<<;

bool operator==(const Token& a, const Token& b) {
  return a.begin == b.begin && a.end == b.end && a.value == b.value;
}

std::ostream& operator<<(std::ostream& os, const Token& t) {
  os << "[token: begin:" << t.begin << ", end: " << t.end
     << ", value: " << t.value << "]";
  return os;
}

std::vector<Token> TokenizeBySpaces(const SingleLine& command) {
  TRACK_OPERATION(TokenizeBySpaces);
  std::vector<Token> output;
  Token token;
  // We have to declare it separate from `token.value`, since here it'll be
  // empty sometimes.
  SingleLine next_token_value;
  auto push = [&](ColumnNumber end) {
    std::visit(
        overload{IgnoreErrors{},
                 [&token, &end, &output](NonEmptySingleLine token_value) {
                   token.end = end;
                   token.value = std::move(token_value);
                   output.push_back(std::move(token));
                 }},
        NonEmptySingleLine::New(std::exchange(next_token_value, SingleLine{})));
    token.begin = ++end;
    token.has_quotes = false;
  };

  ColumnNumber i;
  while (i.ToDelta() < command.size()) {
    ColumnNumber next = FindFirstOf(command, {L' ', L'\"', L'\\'}, i)
                            .value_or(ColumnNumber{} + command.size());
    next_token_value += command.Substring(i, next - i);
    i = next;
    if (i.ToDelta() >= command.size()) continue;
    switch (command.get(i)) {
      case L' ':
        push(i);
        break;
      case L'\"':
        ++i;
        token.has_quotes = true;
        while (i.ToDelta() < command.size() && command.get(i) != '\"') {
          if (command.get(i) == L'\\') {
            ++i;
          }
          if (i.ToDelta() < command.size()) {
            next_token_value +=
                SingleLine{LazyString{ColumnNumberDelta{1}, command.get(i)}};
            ++i;
          }
        }
        break;
      case L'\\':
        ++i;
        token.has_quotes = true;
        if (i.ToDelta() < command.size())
          next_token_value +=
              SingleLine{LazyString{ColumnNumberDelta{1}, command.get(i)}};
        break;
      default:
        LOG(FATAL)
            << "Internal error in TokenizeBySpaces, unexpected character";
    }
    ++i;
  }
  push(ColumnNumber() + command.size());
  return output;
}

PossibleError PushIfNonEmpty(const SingleLine& source, Token token,
                             std::vector<Token>& output) {
  CHECK_LE(token.begin, token.end);
  if (token.begin < token.end) {
    ASSIGN_OR_RETURN(token.value, NonEmptySingleLine::New(source.Substring(
                                      token.begin, token.end - token.begin)));
    output.push_back(std::move(token));
  }
  return EmptyValue{};
}

std::vector<Token> TokenizeGroupsAlnum(const SingleLine& name) {
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
            TokenizeGroupsAlnum(LazyString{L"a\\n\\n\\n\\nf"});
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

std::vector<Token> TokenizeNameForPrefixSearches(const SingleLine& name) {
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
// a corresponding token. If `prefix` is all lower-case, the match ignores case;
// otherwise, it is case-sensitive.
std::optional<Token> FindPrefixInTokens(NonEmptySingleLine prefix,
                                        std::vector<Token> name_tokens) {
  const bool all_lower = prefix == LowerCase(prefix);
  for (const Token& name_token : name_tokens)
    if (StartsWith(all_lower ? LowerCase(name_token.value) : name_token.value,
                   prefix))
      return Token{
          .value = NonEmptySingleLine(name_token.value.read().Substring(
              ColumnNumber(0), ColumnNumberDelta(prefix.size()))),
          .begin = name_token.begin,
          .end = name_token.begin + ColumnNumberDelta(prefix.size())};
  return std::nullopt;
}

const bool get_synthetic_features_tests_registration = tests::Register(
    L"FindPrefixInTokens", std::invoke([] {
      auto test = [](std::wstring name, std::wstring prefix,
                     std::optional<Token> expected_result) {
        return tests::Test{
            .name = name, .callback = [prefix, expected_result] {
              static const NonEmptySingleLine input =
                  NON_EMPTY_SINGLE_LINE_CONSTANT(L"foo/bar/quux/SomethingElse");
              std::optional<Token> result = FindPrefixInTokens(
                  NonEmptySingleLine(SingleLine(LazyString(prefix))),
                  ExtendTokensToEndOfString(
                      input.read(),
                      TokenizeNameForPrefixSearches(input.read())));
              if (result.has_value())
                LOG(INFO) << "Result: " << result.value();
              else
                LOG(INFO) << "No match!";
              CHECK(result == expected_result);
            }};
      };
      return std::vector<tests::Test>{
          test(L"NoMatch", L"xxxx", std::nullopt),
          test(L"LowerCaseIdenticalMatch", L"bar",
               Token{
                   .value = NON_EMPTY_SINGLE_LINE_CONSTANT(L"bar"),
                   .begin = ColumnNumber{4},
                   .end = ColumnNumber{7},
               }),
          test(L"UpperCaseIdenticalMatch", L"Somet",
               Token{
                   .value = NON_EMPTY_SINGLE_LINE_CONSTANT(L"Somet"),
                   .begin = ColumnNumber{13},
                   .end = ColumnNumber{18},
               }),
          test(L"LowerCaseDifferentMatch", L"some",
               Token{
                   .value = NON_EMPTY_SINGLE_LINE_CONSTANT(L"Some"),
                   .begin = ColumnNumber{13},
                   .end = ColumnNumber{17},
               }),
          test(L"UpperCaseDifferentMatch", L"bAr", std::nullopt),
          test(L"NoMatchNotAtStart", L"ome", std::nullopt),
          test(L"MatchAfterCaseSwitch", L"else",
               Token{
                   .value = NON_EMPTY_SINGLE_LINE_CONSTANT(L"Else"),
                   .begin = ColumnNumber{22},
                   .end = ColumnNumber{26},
               }),
      };
    }));
}  // namespace

std::vector<Token> ExtendTokensToEndOfString(SingleLine str,
                                             std::vector<Token> tokens) {
  return container::MaterializeVector(
      tokens | std::views::transform([str](const Token& token) {
        return Token{.value = NonEmptySingleLine{str.Substring(token.begin)},
                     .begin = token.begin,
                     .end = ColumnNumber() + str.size()};
      }));
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

}  // namespace afc::language::lazy_string
