#include "src/vm/escape.h"

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::NonNull;
using afc::language::ValueOrDie;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::ForEachColumn;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Token;
using afc::language::text::LineSequence;

namespace afc::vm {
/* static */ EscapedString EscapedString::FromString(LazyString input) {
  return EscapedString(input);
}

EscapedString::EscapedString(LineSequence input)
    : EscapedString(input.ToLazyString()) {}

/* static */ language::ValueOrError<EscapedString> EscapedString::Parse(
    language::lazy_string::LazyString input) {
  LazyString original_string;
  for (ColumnNumber i; i.ToDelta() < input.size(); ++i) {
    switch (input.get(i)) {
      case '\\':
        if ((++i).ToDelta() >= input.size())
          return Error{LazyString{L"String ends in escape character."}};
        switch (input.get(i)) {
          case 'n':
            original_string += LazyString{L"\n"};
            break;
          case '"':
          case '\\':
          case '\'':
            original_string += input.Substring(i, ColumnNumberDelta(1));
            break;
          default:
            return Error{LazyString{L"Unknown escaped character: "} +
                         input.Substring(i, ColumnNumberDelta(1))};
        }
        break;
      default:
        original_string += input.Substring(i, ColumnNumberDelta(1));
    }
  }
  return EscapedString(original_string);
}

// Returns an escaped representation.
LazyString EscapedString::EscapedRepresentation() const {
  LazyString output;
  ForEachColumn(input_, [&output](ColumnNumber, wchar_t c) {
    switch (c) {
      case '\n':
        output += LazyString{L"\\n"};
        break;
      case '"':
        output += LazyString{L"\\\""};
        break;
      case '\\':
        output += LazyString{L"\\\\"};
        break;
      case '\'':
        output += LazyString{L"\\'"};
        break;
      default:
        output += LazyString{ColumnNumberDelta(1), c};
    }
  });
  return output;
}

LazyString EscapedString::CppRepresentation() const {
  return LazyString{L"\""} + EscapedRepresentation() + LazyString{L"\""};
}

// Returns the original (unescaped) string.
LazyString EscapedString::OriginalString() const { return input_; }

EscapedString::EscapedString(LazyString input) : input_(std::move(input)) {}

namespace {
bool cpp_unescape_string_tests_registration =
    tests::Register(L"EscapedString", [] {
      using ::operator<<;
      auto t = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              CHECK_EQ(
                  LazyString{input},
                  ValueOrDie(EscapedString::Parse(
                                 EscapedString::FromString(LazyString{input})
                                     .EscapedRepresentation()))
                      .OriginalString());
            }};
      };
      auto fail = [](std::wstring name, std::wstring input) {
        return tests::Test{.name = name, .callback = [input] {
                             LOG(INFO) << "Expecting failure from: " << input;
                             CHECK(std::holds_alternative<Error>(
                                 EscapedString::Parse(LazyString{input})));
                           }};
      };
      return std::vector<tests::Test>({
          t(L"EmptyString", L""),
          t(L"Simple", L"Simple"),
          t(L"SingleNewline", L"\n"),
          t(L"EndNewLine", L"foo\n"),
          t(L"StartNewLine", L"\nfoo"),
          t(L"NewlinesInText", L"Foo\nbar\nquux."),
          t(L"SomeQuotes", L"Foo \"with bar\" is 'good'."),
          t(L"SingleBackslash", L"\\"),
          t(L"SomeTextWithBackslash", L"Tab (escaped) is: \\t"),
          fail(L"InvalidEscapeCharacter", L"Foo \\o bar"),
          fail(L"EndsInEscape", L"foo\\"),
      });
    }());
}  // namespace

EscapedMap::EscapedMap(Map input) : input_(std::move(input)) {}

/* static */ language::ValueOrError<EscapedMap> EscapedMap::Parse(
    language::lazy_string::LazyString input) {
  std::multimap<Identifier, LazyString> output;
  for (const Token& token : TokenizeBySpaces(input)) {
    std::optional<ColumnNumber> colon = FindFirstOf(token.value, {L':'});
    if (colon == std::nullopt)
      return Error{
          LazyString{L"Unable to parse map line (no colon found in token): "} +
          input};
    ColumnNumber value_start = token.begin + colon->ToDelta();
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end <= value_start + ColumnNumberDelta(1) ||
        input.get(value_start) != '\"' ||
        input.get(value_end.previous()) != '\"')
      return Error{
          LazyString{L"Unable to parse prompt line (expected quote): "} +
          input};
    // Skip quotes:
    ++value_start;
    --value_end;
    DECLARE_OR_RETURN(Identifier id, Identifier::New(token.value.Substring(
                                         ColumnNumber{0}, colon->ToDelta())));
    output.insert({id, input.Substring(value_start, value_end - value_start)});
  }

  return EscapedMap{output};
}

language::lazy_string::LazyString EscapedMap::Serialize() const {
  return Concatenate(
      input_ |
      std::views::transform([](std::pair<Identifier, LazyString> data) {
        return data.first.read() + LazyString{L":"} +
               EscapedString::FromString(data.second).CppRepresentation();
      }));
}

const EscapedMap::Map& EscapedMap::read() const { return input_; }

}  // namespace afc::vm
