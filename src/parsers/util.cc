#include "src/parsers/util.h"

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/functional.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::NonNull;
using afc::language::container::MaterializeUnorderedSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor::parsers {

// TODO(easy, 2023-09-16): Reuse these symbosl in cpp_parse_tree.
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

static const std::unordered_set<wchar_t> digit_chars =
    MaterializeUnorderedSet(std::wstring_view(L"1234567890."));

namespace {
size_t GetLineHash(const LazyString& line, const std::vector<size_t>& states) {
  using language::compute_hash;
  using language::MakeHashableIteratorRange;
  TRACK_OPERATION(LineOrientedTreeParser_GetLineHash);
  return compute_hash(line, MakeHashableIteratorRange(states));
}
}  // namespace

void ParseQuotedString(ParseData* result, wchar_t quote_char,
                       LineModifierSet string_modifiers,
                       std::unordered_set<ParseTreeProperty> properties) {
  ParseQuotedString(result, quote_char, string_modifiers, properties,
                    std::nullopt);
}

void ParseQuotedString(
    ParseData* result, wchar_t quote_char, LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties,
    std::optional<NestedExpressionSyntax> nested_expression_syntax) {
  LineColumn original_position = result->position();
  CHECK_GT(original_position.column, ColumnNumber(0));

  Seek seek = result->seek();
  // If nested_expression_syntax is present, holds alternating positions of the
  // start of a prefix and a suffix.
  std::vector<ColumnNumber> nested_expression_columns;

  while (seek.read() != quote_char && seek.read() != L'\n' &&
         !seek.AtRangeEnd()) {
    if (seek.read() == '\\') {
      seek.Once();
    } else if (nested_expression_syntax.has_value()) {
      const NonEmptySingleLine& token =
          nested_expression_columns.size() % 2 == 0
              ? nested_expression_syntax->prefix
              : nested_expression_syntax->suffix;
      if (seek.Matches(token)) {
        nested_expression_columns.push_back(result->position().column);
        result->set_position(result->position() + token.size());
        continue;
      }
    }
    seek.Once();
  }

  if (seek.read() != quote_char) {
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
    return;
  }

  const LineColumn final_quote_position = result->position();
  CHECK_EQ(result->position().line, original_position.line);

  static const size_t kIgnoredState = 0;

  // Parent tree: a parent tree containing everything.
  seek.Once();  // Consume the closing quote
  result->Push(kIgnoredState,
               final_quote_position.column + ColumnNumberDelta(1) -
                   original_position.column,
               {}, {});

  // Open quote.
  result->set_position(original_position);
  result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

  LineColumn content_start_position = original_position;
  result->set_position(content_start_position);

  for (size_t i = 0; i < nested_expression_columns.size(); ++i) {
    ColumnNumber token_position = nested_expression_columns[i];
    bool at_prefix = i % 2 == 0;

    if (token_position > result->position().column) {
      // Contents before token.
      ColumnNumberDelta len = token_position - result->position().column;
      CHECK_GE(len, ColumnNumberDelta{});
      result->set_position(LineColumn{result->position().line, token_position});
      result->PushAndPop(
          len,
          at_prefix ? string_modifiers : nested_expression_syntax->modifiers,
          at_prefix ? properties : std::unordered_set<ParseTreeProperty>{});
    }

    const NonEmptySingleLine& token = at_prefix
                                          ? nested_expression_syntax->prefix
                                          : nested_expression_syntax->suffix;
    // Token:
    result->set_position(result->position() + token.size());
    result->PushAndPop(token.size(),
                       nested_expression_syntax->prefix_suffix_modifiers, {});
  }

  // Remaining content after all nested expressions before the closing quote.
  ColumnNumberDelta len =
      final_quote_position.column - result->position().column;
  if (len > ColumnNumberDelta{}) {
    result->set_position(final_quote_position);
    result->PushAndPop(len, string_modifiers, properties);
  }

  // Close quote.
  result->set_position(final_quote_position + ColumnNumberDelta{1});
  result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

  result->PopBack();  // Parent tree.
  // TODO: words_parser_->FindChildren(result->contents(), tree);
}

void ParseNumber(ParseData* result, LineModifierSet number_modifiers,
                 std::unordered_set<ParseTreeProperty> properties) {
  CHECK_GE(result->position().column, ColumnNumber(1));
  LineColumn original_position = result->position();
  original_position.column--;

  result->seek().UntilCurrentCharNotIn(digit_chars);
  CHECK_EQ(result->position().line, original_position.line);
  CHECK_GT(result->position(), original_position);

  result->PushAndPop(result->position().column - original_position.column,
                     number_modifiers, properties);
}

ParseTree LineOrientedTreeParser::FindChildren(const LineSequence& contents,
                                               Range range) {
  TRACK_OPERATION(LineOrientedTreeParser_FindChildren);
  cache_.SetMaxSize(contents.size().read());

  std::vector<size_t> states_stack = {kDefaultState};
  std::vector<ParseTree> trees = {ParseTree(range)};

  range.ForEachLine([&](LineNumber i) {
    size_t hash = GetLineHash(contents.at(i).contents().read(), states_stack);
    NonNull<const ParseResults*> parse_results = cache_.Get(hash, [&] {
      TRACK_OPERATION(LineOrientedTreeParser_FindChildren_Parse);
      ParseData data(contents, std::move(states_stack),
                     std::min(LineColumn(i + LineNumberDelta(1)), range.end()));
      data.set_position(std::max(LineColumn(i), range.begin()));
      ParseLine(&data);
      return data.parse_results();
    });

    TRACK_OPERATION(LineOrientedTreeParser_FindChildren_ExecuteActions);
    CHECK(!trees.empty());
    for (const auto& action : parse_results->actions)
      Execute(action, &trees, i);
    states_stack = parse_results->states_stack;
  });

  auto final_position =
      LineColumn(contents.EndLine(), contents.back().EndColumn());
  if (final_position >= range.end()) {
    DVLOG(5) << "Draining final states: " << states_stack.size();
    ParseData data(contents, std::move(states_stack),
                   std::min(LineColumn(LineNumber(0) + contents.size() +
                                       LineNumberDelta(1)),
                            range.end()));
    while (data.parse_results().states_stack.size() > 1) {
      data.PopBack();
    }
    for (const auto& action : data.parse_results().actions) {
      Execute(action, &trees, final_position.line);
    }
  }
  CHECK(!trees.empty());
  return trees[0];
}

}  // namespace afc::editor::parsers
