#include "src/parsers/util.h"

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/functional.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor::parsers {

// TODO(easy, 2023-09-16): Reuse these symbosl in cpp_parse_tree.
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

static const std::wstring digit_chars = L"1234567890.";

namespace {
size_t GetLineHash(const LazyString& line, const std::vector<size_t>& states) {
  using language::compute_hash;
  using language::MakeHashableIteratorRange;
  TRACK_OPERATION(LineOrientedTreeParser_GetLineHash);
  return compute_hash(line, MakeHashableIteratorRange(states));
}
}  // namespace

void ParseDoubleQuotedString(ParseData* result,
                             LineModifierSet string_modifiers,
                             std::unordered_set<ParseTreeProperty> properties) {
  LineColumn original_position = result->position();
  CHECK_GT(original_position.column, ColumnNumber(0));

  Seek seek = result->seek();
  while (seek.read() != L'"' && seek.read() != L'\n' && !seek.AtRangeEnd()) {
    if (seek.read() == '\\') {
      seek.Once();
    }
    seek.Once();
  }
  if (seek.read() == L'"') {
    LineColumn final_quote_position = result->position();
    CHECK_EQ(result->position().line, original_position.line);

    size_t ignored_state = 0;

    // Parent tree: a parent tree containing everything.
    seek.Once();
    result->Push(ignored_state,
                 final_quote_position.column + ColumnNumberDelta(2) -
                     original_position.column,
                 {}, {});

    // Open quote.
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

    // Content tree.
    result->set_position(final_quote_position);
    result->PushAndPop(final_quote_position.column - original_position.column,
                       string_modifiers, properties);

    // Close quote.
    seek.Once();
    result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim}, {});

    result->PopBack();  // Parent tree.
    // TODO: words_parser_->FindChildren(result->contents(), tree);
  } else {
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
  }
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
    size_t hash = GetLineHash(contents.at(i).contents(), states_stack);
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
