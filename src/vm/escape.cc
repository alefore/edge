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
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;

namespace afc::vm {
/* static */ EscapedString EscapedString::FromString(LazyString input) {
  return EscapedString(input);
}

/* static */ language::ValueOrError<EscapedString> EscapedString::Parse(
    language::lazy_string::LazyString input) {
  TRACK_OPERATION(EscapedString_Parse);
  LazyString original_string;
  ColumnNumber position;
  while (position.ToDelta() < input.size()) {
    std::optional<ColumnNumber> escape = FindFirstOf(input, {L'\\'}, position);
    original_string += input.Substring(
        position,
        (escape.has_value() ? escape.value() : ColumnNumber{} + input.size()) -
            position);
    position = escape.value_or(ColumnNumber{} + input.size());
    if (escape.has_value()) {
      if ((++position).ToDelta() >= input.size())
        return Error{LazyString{L"String ends in escape character."}};
      switch (input.get(position)) {
        case 'n':
          original_string += LazyString{L"\n"};
          break;
        case '"':
        case '\\':
        case '\'':
          original_string += input.Substring(position, ColumnNumberDelta(1));
          break;
        default:
          return Error{LazyString{L"Unknown escaped character: "} +
                       input.Substring(position, ColumnNumberDelta(1))};
      }
      ++position;
    }
  }
  return EscapedString(original_string);
}

// Returns an escaped representation.
SingleLine EscapedString::EscapedRepresentation() const {
  SingleLine output;
  ForEachColumn(read(), [&output](ColumnNumber, wchar_t c) {
    switch (c) {
      case '\n':
        output += SingleLine{LazyString{L"\\n"}};
        break;
      case '"':
        output += SingleLine{LazyString{L"\\\""}};
        break;
      case '\\':
        output += SingleLine{LazyString{L"\\\\"}};
        break;
      case '\'':
        output += SingleLine{LazyString{L"\\'"}};
        break;
      default:
        output += SingleLine{LazyString{ColumnNumberDelta(1), c}};
    }
  });
  return output;
}

SingleLine EscapedString::CppRepresentation() const {
  return SingleLine{LazyString{L"\""}} + EscapedRepresentation() +
         SingleLine{LazyString{L"\""}};
}

// Returns the original (unescaped) string.
LazyString EscapedString::OriginalString() const { return read(); }

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
                                     .EscapedRepresentation()
                                     .read()))
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
  TRACK_OPERATION(EscapedMap_Parse);
  EscapedMap::Map output;
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
    DECLARE_OR_RETURN(EscapedString parsed_value,
                      EscapedString::Parse(input.Substring(
                          value_start, value_end - value_start)));
    output.insert({id, parsed_value});
  }

  return EscapedMap{output};
}

LazyString EscapedMap::Serialize() const {
  return Concatenate(
      input_ |
      std::views::transform(
          [](std::pair<Identifier, EscapedString> data) -> LazyString {
            // TODO(trivial, 2024-09-16): Change Identifier to
            // SingleLine, avoid wrapping.
            // TODO(trivial, 2024-09-16): Return a SingleLine.
            return (SingleLine{data.first.read()} +
                    SingleLine{LazyString{L":"}} +
                    data.second.CppRepresentation())
                .read();
          }) |
      Intersperse(LazyString{L" "}));
}

const EscapedMap::Map& EscapedMap::read() const { return input_; }

}  // namespace afc::vm
