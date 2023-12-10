#include "src/token_predictor.h"

#include <ranges>
#include <vector>

#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/text/line.h"

using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Substring;
using afc::language::lazy_string::Token;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;

namespace afc::editor {
namespace {
LineSequence TransformLines(const NonNull<std::shared_ptr<LazyString>>& input,
                            const Token& token_to_expand, LineSequence lines) {
  LineBuilder head(input);
  head.DeleteSuffix(token_to_expand.begin);

  LineBuilder tail(input);
  tail.DeleteCharacters(ColumnNumber(), token_to_expand.end.ToDelta());
  return lines.Map(
      [&](const NonNull<std::shared_ptr<const Line>>& expanded_token) {
        LineBuilder output;
        output.Append(head.Copy());
        output.Append(LineBuilder(expanded_token.value()));
        output.Append(tail.Copy());
        return MakeNonNullShared<const Line>(std::move(output).Build());
      });
}
}  // namespace

Predictor TokenPredictor(Predictor predictor) {
  return [predictor](PredictorInput input) {
    std::vector<Token> tokens = TokenizeBySpaces(input.input.value());

    if (auto it = std::ranges::find_if(tokens,
                                       [&](Token& token) {
                                         return token.begin <=
                                                    input.input_column &&
                                                token.end > input.input_column;
                                       });
        it != tokens.end()) {
      Token token_to_expand = *it;
      input.input_column -= token_to_expand.begin;
      input.input = NewLazyString(token_to_expand.value);
      return predictor(input).Transform(
          [input_value = input.input, token_to_expand](PredictorOutput output) {
            LineSequence transformed_output =
                TransformLines(input_value, token_to_expand,
                               output.contents.sorted_lines().lines());
            output.contents = SortedLineSequenceUniqueLines(
                language::text::SortedLineSequence(
                    std::move(transformed_output)));

            return futures::Past(output);
          });
    }
    return predictor(input);
  };
}
}  // namespace afc::editor
