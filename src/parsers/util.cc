#include "src/parsers/util.h"

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/functional.h"

namespace afc::editor::parsers {
using infrastructure::Tracker;
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::Range;

// TODO(easy, 2023-09-16): Reuse these symbosl in cpp_parse_tree.
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

static const std::wstring digit_chars = L"1234567890.";

namespace {
size_t GetLineHash(const LazyString& line, const std::vector<size_t>& states) {
  using language::compute_hash;
  using language::MakeHashableIteratorRange;
  static Tracker tracker(L"CppTreeParser::GetLineHash");
  auto call = tracker.Call();
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
    seek.Once();
    CHECK_EQ(result->position().line, original_position.line);
    result->PushAndPop(result->position().column - original_position.column +
                           ColumnNumberDelta(1),
                       string_modifiers, properties);
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
  static Tracker top_tracker(L"CppTreeParser::FindChildren");
  auto top_call = top_tracker.Call();
  cache_.SetMaxSize(contents.size().read());

  std::vector<size_t> states_stack = {kDefaultState};
  std::vector<ParseTree> trees = {ParseTree(range)};

  range.ForEachLine([&](LineNumber i) {
    size_t hash = GetLineHash(contents.at(i)->contents().value(), states_stack);
    auto parse_results = cache_.Get(hash, [&] {
      static Tracker tracker(L"CppTreeParser::FindChildren::Parse");
      auto call = tracker.Call();
      ParseData data(contents, std::move(states_stack),
                     std::min(LineColumn(i + LineNumberDelta(1)), range.end));
      data.set_position(std::max(LineColumn(i), range.begin));
      ParseLine(&data);
      return *data.parse_results();
    });

    static Tracker execute_tracker(
        L"CppTreeParser::FindChildren::ExecuteActions");
    auto execute_call = execute_tracker.Call();
    for (auto& action : parse_results->actions) {
      action.Execute(&trees, i);
    }
    states_stack = parse_results->states_stack;
  });

  auto final_position =
      LineColumn(contents.EndLine(), contents.back()->EndColumn());
  if (final_position >= range.end) {
    DVLOG(5) << "Draining final states: " << states_stack.size();
    ParseData data(contents, std::move(states_stack),
                   std::min(LineColumn(LineNumber(0) + contents.size() +
                                       LineNumberDelta(1)),
                            range.end));
    while (data.parse_results()->states_stack.size() > 1) {
      data.PopBack();
    }
    for (auto& action : data.parse_results()->actions) {
      action.Execute(&trees, final_position.line);
    }
  }
  CHECK(!trees.empty());
  return trees[0];
}

}  // namespace afc::editor::parsers
