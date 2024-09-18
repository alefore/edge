#include "src/token_predictor.h"

#include <glog/logging.h>

#include <ranges>
#include <vector>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;

namespace afc::editor {
namespace {
using ::operator<<;

std::optional<Token> FindToken(std::vector<Token> tokens, ColumnNumber column) {
  LOG(INFO) << "Tokens: " << tokens.size();
  if (auto it = std::ranges::find_if(tokens,
                                     [&](Token& token) {
                                       return token.begin <= column &&
                                              token.end >= column;
                                     });
      it != tokens.end())
    return *it;
  return std::nullopt;
}

bool find_token_tests = tests::Register(
    L"FindToken",
    {{.name = L"Empty",
      .callback =
          [] {
            CHECK(!FindToken(TokenizeBySpaces(LazyString()), ColumnNumber())
                       .has_value());
          }},
     {.name = L"SpacesInTheMiddle",
      .callback =
          [] {
            CHECK(!FindToken(TokenizeBySpaces(LazyString{L"012    89"}),
                             ColumnNumber(15))
                       .has_value());
          }},
     {.name = L"MiddleSecondToken",
      .callback =
          [] {
            CHECK_EQ(
                FindToken(TokenizeBySpaces(LazyString{L"01234 678901 345678"}),
                          ColumnNumber(8))
                    .value(),
                Token({.value = LazyString{L"678901"},
                       .begin = ColumnNumber(6),
                       .end = ColumnNumber(12)}));
          }},
     {.name = L"EndSecondToken",
      .callback =
          [] {
            CHECK_EQ(
                FindToken(TokenizeBySpaces(LazyString{L"01234 678901 345678"}),
                          ColumnNumber(12))
                    .value(),
                Token({.value = LazyString{L"678901"},
                       .begin = ColumnNumber(6),
                       .end = ColumnNumber(12)}));
          }},
     {.name = L"BeginThirdToken",
      .callback =
          [] {
            CHECK_EQ(
                FindToken(TokenizeBySpaces(LazyString{L"01234 678901 345678"}),
                          ColumnNumber(13))
                    .value(),
                Token({.value = LazyString{L"345678"},
                       .begin = ColumnNumber(13),
                       .end = ColumnNumber(19)}));
          }},
     {.name = L"MiddleLastToken",
      .callback =
          [] {
            CHECK_EQ(
                FindToken(TokenizeBySpaces(LazyString{L"01234 678901 345678"}),
                          ColumnNumber(15))
                    .value(),
                Token({.value = LazyString{L"345678"},
                       .begin = ColumnNumber(13),
                       .end = ColumnNumber(19)}));
          }},
     {.name = L"EndOfString", .callback = [] {
        CHECK_EQ(FindToken(TokenizeBySpaces(LazyString{L"01234 678901"}),
                           ColumnNumber(12))
                     .value(),
                 Token({.value = LazyString{L"678901"},
                        .begin = ColumnNumber(6),
                        .end = ColumnNumber(12)}));
      }}});

// Transforms a sequence of expansions for a token inside an input into a
// sequence of expansions for the entire input.
//
// For example, if the input is "foo src/buf blah" (3 tokens) and the token
// being expanded is "src/buf" (the 2nd token), `lines` will contains strings
// like "src/buffer.cc" and "src/buffer.h" corresponding to the expansions found
// for the token. The output will contain strings like "foo src/buffer.cc blah".
//
// Arguments:
// * `input`: The original string containing multiple tokens, one of which was
//   expanded.
// * `token_expanded`: The token that was expanded.
// * `lines`: A sequence of lines found that are suitable to expand the token.
SortedLineSequenceUniqueLines TransformLines(const SingleLine& input,
                                             const Token& token,
                                             LineSequence lines) {
  LineBuilder head{input};
  head.DeleteSuffix(token.begin);

  LineBuilder tail{input};
  tail.DeleteCharacters(ColumnNumber(), token.end.ToDelta());
  return SortedLineSequenceUniqueLines(
      SortedLineSequence(lines.Map([&](const Line& expansion) -> Line {
        if (expansion.empty()) return expansion;
        LineBuilder output;
        output.Append(head.Copy());
        output.Append(LineBuilder(expansion));
        output.Append(tail.Copy());
        return std::move(output).Build();
      })));
}

bool transform_lines_tests = tests::Register(
    L"TransformLines",
    {{.name = L"BasicFunctionality",
      .callback =
          [] {
            SortedLineSequenceUniqueLines result = TransformLines(
                SingleLine{LazyString{L"foo src/buf blah"}},
                Token{.value = LazyString{L"src/buf"},
                      .begin = ColumnNumber(4),
                      .end = ColumnNumber(11)},
                LineSequence::ForTests({L"src/buffer.cc", L"src/buffer.h"}));
            LineSequence expected = LineSequence::ForTests(
                {L"foo src/buffer.cc blah", L"foo src/buffer.h blah"});
            CHECK(result.read().lines() == expected);
          }},
     {.name = L"SingleToken",
      .callback =
          [] {
            SortedLineSequenceUniqueLines result = TransformLines(
                SingleLine{LazyString{L"src/buf"}},
                Token{.value = LazyString{L"src/buf"},
                      .begin = ColumnNumber(0),
                      .end = ColumnNumber(7)},
                LineSequence::ForTests({L"src/buffer.cc", L"src/buffer.h"}));
            LineSequence expected =
                LineSequence::ForTests({L"src/buffer.cc", L"src/buffer.h"});
            CHECK(result.read().lines() == expected);
          }},
     {.name = L"RepeatedTokenSpecificExpansion",
      .callback =
          [] {
            SortedLineSequenceUniqueLines result = TransformLines(
                SingleLine{LazyString{L"src/buf and again src/buf"}},
                Token{.value = LazyString{L"src/buf"},
                      .begin = ColumnNumber(18),
                      .end = ColumnNumber(25)},
                LineSequence::ForTests({L"src/buffer.cc"}));
            LineSequence expected =
                LineSequence::ForTests({L"src/buf and again src/buffer.cc"});
            CHECK(result.read().lines() == expected);
          }},
     {.name = L"ExactMatchLinesSequence", .callback = [] {
        SortedLineSequenceUniqueLines result =
            TransformLines(SingleLine{LazyString{L"foo src/buf blah"}},
                           Token{.value = LazyString{L"src/buf"},
                                 .begin = ColumnNumber(4),
                                 .end = ColumnNumber(11)},
                           LineSequence::ForTests({L"src/buf"}));
        LineSequence expected = LineSequence::ForTests({L"foo src/buf blah"});
        CHECK(result.read().lines() == expected);
      }}});
}  // namespace

Predictor TokenPredictor(Predictor predictor) {
  return [predictor](PredictorInput input) {
    LOG(INFO) << "Token Predictor: " << input.input_column;
    return VisitOptional(
        [&input, &predictor](Token token_to_expand) {
          LOG(INFO) << "Found token: " << token_to_expand;
          input.input_column =
              input.input_column - token_to_expand.begin.ToDelta();
          // TODO(easy, 2024-09-18): Avoid conversion to
          // LazyString->SingleLine here:
          SingleLine original_input = std::exchange(
              input.input, SingleLine{LazyString{token_to_expand.value}});
          return predictor(input).Transform([original_input, token_to_expand](
                                                PredictorOutput output) {
            return futures::Past(PredictorOutput{
                .longest_prefix =
                    output.longest_prefix + token_to_expand.begin.ToDelta(),
                .longest_directory_match = output.longest_directory_match +
                                           token_to_expand.begin.ToDelta(),
                .found_exact_match = output.found_exact_match,
                .contents = TransformLines(original_input, token_to_expand,
                                           output.contents.read().lines())});
          });
        },
        [&input, &predictor] {
          LOG(INFO) << "No expansion.";
          return predictor(input);
        },
        // TODO(trivial, 2024-09-18): Avoid `read`?
        FindToken(TokenizeBySpaces(input.input.read()), input.input_column));
  };
}
}  // namespace afc::editor
