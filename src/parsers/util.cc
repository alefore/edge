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

ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char,
    infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties) {
  return ParseQuotedString(result, quote_char, std::move(string_modifiers),
                           std::move(properties), MultipleLinesSupport::kReject,
                           CurrentState::kStart);
}

ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char, LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties,
    MultipleLinesSupport multiple_lines_support, CurrentState current_state) {
  return ParseQuotedString(result, quote_char, string_modifiers, properties,
                           std::nullopt, multiple_lines_support, current_state);
}

ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char, LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties,
    std::optional<NestedExpressionSyntax> nested_expression_syntax,
    MultipleLinesSupport multiple_lines_support, CurrentState current_state) {
  LineColumn original_position = result->position();
  if (current_state == CurrentState::kStart)
    CHECK_GT(original_position.column, ColumnNumber{0});

  Seek seek = result->seek();
  // If nested_expression_syntax is present, holds alternating positions of the
  // start of a prefix and a suffix.
  std::vector<ColumnNumber> nested_expression_columns;
  if (current_state == CurrentState::kContinuationInNestedExpression)
    nested_expression_columns.push_back(ColumnNumber{});  // Dummy value.

  LOG(INFO) << "XXXX: Start first loop.";
  while (seek.read() != quote_char && seek.read() != L'\n' &&
         !seek.AtRangeEnd()) {
    if (seek.read() == L'\\') {
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

  LOG(INFO) << "XXXX: After first loop.";
  bool at_quote = seek.read() == quote_char;

  if (!at_quote && multiple_lines_support == MultipleLinesSupport::kReject) {
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
    return ParseQuotedStringState::kDone;
  }

  const LineColumn final_position = result->position();
  result->set_position(original_position);
  if (current_state == CurrentState::kStart) {
    // Open quote.
    result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

    // Parent tree: a parent tree containing everything.
    static const size_t kIgnoredState = 0;
    result->Push(kIgnoredState, ColumnNumberDelta{}, {}, {});
  }
  for (size_t i = 0; i < nested_expression_columns.size(); ++i) {
    if (i == 0 &&
        current_state == CurrentState::kContinuationInNestedExpression)
      continue;  // Skip dummy value.

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

  // Remaining content after all nested expressions before the closing
  // quote.
  ColumnNumberDelta len = final_position.column - result->position().column;
  if (len > ColumnNumberDelta{}) {
    result->set_position(final_position);
    result->PushAndPop(len,
                       nested_expression_columns.size() % 2 == 0
                           ? string_modifiers
                           : nested_expression_syntax->modifiers,
                       nested_expression_columns.size() % 2 == 0
                           ? properties
                           : std::unordered_set<ParseTreeProperty>{});
  }

  if (!at_quote)
    return nested_expression_columns.size() % 2 == 0
               ? ParseQuotedStringState::kInDefaultState
               : ParseQuotedStringState::kInNestedExpression;

  CHECK_EQ(result->position().line, original_position.line);

  result->PopBack();  // Parent tree.

  // Close quote.
  result->set_position(final_position + ColumnNumberDelta{1});
  result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

  // TODO: words_parser_->FindChildren(result->contents(), tree);
  return ParseQuotedStringState::kDone;
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
