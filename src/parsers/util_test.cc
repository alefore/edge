#include "src/parsers/util.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/range.h"
#include "src/parse_tools.h"
#include "src/parse_tree.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor::parsers {

using ::operator<<;

tests::Test TestParseQuotedString(
    const std::wstring& name, const std::wstring& input_string,
    wchar_t quote_char, const LineModifierSet& kContentModifiers,
    const std::unordered_set<ParseTreeProperty>& kContentProperties,
    std::optional<NestedExpressionSyntax> nested_expression_syntax,
    MultipleLinesSupport multiple_lines_support, CurrentState initial_state,
    ParseQuotedStringState expected_state, ParseTree expected_tree) {
  return tests::Test{
      .name = name, .callback = [=] {
        // We simulate that the line tested is LineNumber{1}. The document
        // starts on LineNumber{1} and ends on LineNumber{2}.
        LineSequence contents = LineSequence::ForTests({L"", input_string});

        ParseData parse_data(contents, {}, LineColumn{LineNumber{2}});

        if (initial_state == CurrentState::kStart) {
          // Skip the opening quote.
          parse_data.set_position(LineColumn{LineNumber{1}, ColumnNumber{1}});
        } else {
          static const int kIgnoredState = 0;
          // Push the parent parse tree.
          parse_data.Push(kIgnoredState, ColumnNumberDelta{}, {}, {});
          parse_data.set_position(LineColumn{LineNumber{1}, ColumnNumber{0}});
        }

        CHECK_EQ(ParseQuotedString(&parse_data, quote_char, kContentModifiers,
                                   kContentProperties, nested_expression_syntax,
                                   multiple_lines_support, initial_state),
                 expected_state);

        std::vector<ParseTree> parse_trees = {
            ParseTree(Range(LineColumn{LineNumber{0}, ColumnNumber{0}},
                            LineColumn{LineNumber{2}, ColumnNumber{0}}))};

        for (const auto& action : parse_data.parse_results().actions)
          Execute(action, &parse_trees, LineNumber{1});

        // Assume that all trees end on the next line.
        while (parse_trees.size() > 1) {
          LOG(INFO) << "Pop parse tree: " << parse_trees.back();
          Execute(ActionPop{.column = ColumnNumber{}}, &parse_trees,
                  LineNumber{2});
        }
        CHECK_EQ(parse_trees[0], expected_tree);
      }};
}

ParseTree NewTree(Range range, LineModifierSet modifiers,
                  std::unordered_set<ParseTreeProperty> properties,
                  std::vector<ParseTree> children) {
  ParseTree output(range);
  output.set_modifiers(modifiers);
  output.set_properties(properties);
  for (ParseTree& c : children) output.PushChild(c);
  return output;
}

ParseTree ContainerTree(std::vector<ParseTree> children) {
  return NewTree(Range{LineColumn{LineNumber{0}}, LineColumn{LineNumber{2}}},
                 {}, {}, children);
}

// Helper function to create a parse tree node and append it to a parent tree.
// This version assumes a single line (LineNumber{1}).
ParseTree NewTree(ColumnNumber begin, SingleLine contents,
                  LineModifierSet modifiers,
                  std::unordered_set<ParseTreeProperty> properties,
                  std::vector<ParseTree> children) {
  return NewTree(Range(LineColumn(LineNumber{1}, begin),
                       LineColumn(LineNumber{1}, begin + contents.size())),
                 modifiers, properties, children);
}

ParseTree StringParentTree(ColumnNumber start_position,
                           std::vector<ParseTree> children) {
  return NewTree(Range{LineColumn{LineNumber{1}, start_position},
                       LineColumn{LineNumber{2}}},
                 {}, {}, children);
}

ParseTree StringParentContinuationTree(std::vector<ParseTree> children) {
  return StringParentTree(ColumnNumber{0}, children);
}

ParseTree OpeningQuote() {
  return NewTree(ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"\""),
                 LineModifierSet{LineModifier::kDim}, {}, {});
}

ParseTree ClosingQuote(ColumnNumber position) {
  return NewTree(position, SINGLE_LINE_CONSTANT(L"\""),
                 LineModifierSet{LineModifier::kDim}, {}, {});
}

static const NonEmptySingleLine kNestedSyntaxPrefix =
    NON_EMPTY_SINGLE_LINE_CONSTANT(L"{");
static const NonEmptySingleLine kNestedSyntaxSuffix =
    NON_EMPTY_SINGLE_LINE_CONSTANT(L"}");
static const LineModifierSet kContentModifiers = {LineModifier::kBold};
static const std::unordered_set<ParseTreeProperty> kContentProperties = {
    ParseTreeProperty::StringValue()};
LineModifierSet kNestedPrefixSuffixModifiers = {LineModifier::kDim};
LineModifierSet kNestedContentModifiers = {LineModifier::kGreen};

bool parse_quoted_string_tests = afc::tests::Register(
    L"ParseQuotedString",
    std::vector<afc::tests::Test>{
        TestParseQuotedString(
            L"EmptyString", L"\"\"", L'\"', kContentModifiers,
            kContentProperties, std::nullopt, MultipleLinesSupport::kReject,
            CurrentState::kStart, ParseQuotedStringState::kDone,
            ContainerTree({OpeningQuote(),
                           NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L""),
                                   {}, {}, {}),
                           ClosingQuote(ColumnNumber{1})})),
        TestParseQuotedString(
            L"SimpleString", L"\"hello\"", L'\"', kContentModifiers,
            kContentProperties, std::nullopt, MultipleLinesSupport::kReject,
            CurrentState::kStart, ParseQuotedStringState::kDone,
            ContainerTree(
                {OpeningQuote(),
                 NewTree(
                     ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"hello"), {}, {},
                     {NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"hello"),
                              kContentModifiers, kContentProperties, {})}),
                 ClosingQuote(ColumnNumber{6})})),
        TestParseQuotedString(
            L"NestedExpressionString", L"\"foo {expr} bar\"", L'\"',
            kContentModifiers, kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kReject, CurrentState::kStart,
            ParseQuotedStringState::kDone,
            ContainerTree(
                {OpeningQuote(),
                 NewTree(
                     ColumnNumber{1},
                     SINGLE_LINE_CONSTANT(L"foo {expr} bar"), {}, {},
                     {
                         NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"foo "),
                                 kContentModifiers, kContentProperties, {}),
                         NewTree(ColumnNumber{5}, kNestedSyntaxPrefix.read(),
                                 kNestedPrefixSuffixModifiers, {}, {}),
                         NewTree(ColumnNumber{6}, SINGLE_LINE_CONSTANT(L"expr"),
                                 kNestedContentModifiers, {}, {}),
                         NewTree(ColumnNumber{10}, kNestedSyntaxSuffix.read(),
                                 kNestedPrefixSuffixModifiers, {}, {}),
                         NewTree(ColumnNumber{11},
                                 SINGLE_LINE_CONSTANT(L" bar"),
                                 kContentModifiers, kContentProperties, {}),
                     }),
                 ClosingQuote(ColumnNumber{15})})),
        TestParseQuotedString(
            L"MultipleNestedExpressions",
            L"\"foo {i + 1} bar quux {some.call()} nah\"", L'\"',
            kContentModifiers, kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kReject, CurrentState::kStart,
            ParseQuotedStringState::kDone,
            ContainerTree(
                {OpeningQuote(),
                 NewTree(ColumnNumber{1},
                         SINGLE_LINE_CONSTANT(
                             L"foo {i + 1} bar quux {some.call()} nah"),
                         {}, {},
                         {
                             NewTree(ColumnNumber{1},
                                     SINGLE_LINE_CONSTANT(L"foo "),
                                     kContentModifiers, kContentProperties, {}),
                             NewTree(ColumnNumber{5},
                                     kNestedSyntaxPrefix.read(),
                                     kNestedPrefixSuffixModifiers, {}, {}),
                             NewTree(ColumnNumber{6},
                                     SINGLE_LINE_CONSTANT(L"i + 1"),
                                     kNestedContentModifiers, {}, {}),
                             NewTree(ColumnNumber{11},
                                     kNestedSyntaxSuffix.read(),
                                     kNestedPrefixSuffixModifiers, {}, {}),
                             NewTree(ColumnNumber{12},
                                     SINGLE_LINE_CONSTANT(L" bar quux "),
                                     kContentModifiers, kContentProperties, {}),
                             NewTree(ColumnNumber{22},
                                     kNestedSyntaxPrefix.read(),
                                     kNestedPrefixSuffixModifiers, {}, {}),
                             NewTree(ColumnNumber{23},
                                     SINGLE_LINE_CONSTANT(L"some.call()"),
                                     kNestedContentModifiers, {}, {}),
                             NewTree(ColumnNumber{34},
                                     kNestedSyntaxSuffix.read(),
                                     kNestedPrefixSuffixModifiers, {}, {}),
                             NewTree(ColumnNumber{35},
                                     SINGLE_LINE_CONSTANT(L" nah"),
                                     kContentModifiers, kContentProperties, {}),
                         }),
                 ClosingQuote(ColumnNumber{39})})),

        // --- Multi-line tests without nested expressions ---

        TestParseQuotedString(
            L"MultiLine_NoNested_StartsAndDoesntFinish",
            L"\"first line of string", L'\"', kContentModifiers,
            kContentProperties, std::nullopt, MultipleLinesSupport::kAccept,
            CurrentState::kStart, ParseQuotedStringState::kInDefaultState,
            ContainerTree(
                {OpeningQuote(),
                 StringParentTree(
                     ColumnNumber{1},
                     {NewTree(ColumnNumber{1},
                              SINGLE_LINE_CONSTANT(L"first line of string"),
                              kContentModifiers, kContentProperties, {})})})),

        TestParseQuotedString(
            L"MultiLine_NoNested_ContinuationLine", L"second line of string",
            L'\"', kContentModifiers, kContentProperties, std::nullopt,
            MultipleLinesSupport::kAccept, CurrentState::kContinuationInDefault,
            ParseQuotedStringState::kInDefaultState,
            ContainerTree({StringParentContinuationTree({NewTree(
                ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"second line of string"),
                kContentModifiers, kContentProperties, {})})})),
        TestParseQuotedString(
            L"MultiLine_NoNested_ContinuationFinishes",
            L"third line of string\"", L'\"', kContentModifiers,
            kContentProperties, std::nullopt, MultipleLinesSupport::kAccept,
            CurrentState::kContinuationInDefault, ParseQuotedStringState::kDone,
            ContainerTree(
                {NewTree(ColumnNumber{0},
                         SINGLE_LINE_CONSTANT(L"third line of string"), {}, {},
                         {NewTree(ColumnNumber{0},
                                  SINGLE_LINE_CONSTANT(L"third line of string"),
                                  kContentModifiers, kContentProperties, {})}),
                 ClosingQuote(ColumnNumber{20})})),

        // --- Multi-line tests with nested expressions ---

        TestParseQuotedString(
            L"MultiLine_Nested_StartsInLineEndsInNested", L"\"foo {expr_start",
            L'\"', kContentModifiers, kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept, CurrentState::kStart,
            ParseQuotedStringState::kInNestedExpression,
            ContainerTree(
                {OpeningQuote(),
                 StringParentTree(
                     ColumnNumber{1},
                     {
                         NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"foo "),
                                 kContentModifiers, kContentProperties, {}),
                         NewTree(ColumnNumber{5}, kNestedSyntaxPrefix.read(),
                                 kNestedPrefixSuffixModifiers, {}, {}),
                         NewTree(ColumnNumber{6},
                                 SINGLE_LINE_CONSTANT(L"expr_start"),
                                 kNestedContentModifiers, {}, {}),
                     })})),

        TestParseQuotedString(
            L"MultiLine_Nested_ContinuationInNestedEndsInNested",
            L"more nested content", L'\"', kContentModifiers,
            kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept,
            CurrentState::kContinuationInNestedExpression,
            ParseQuotedStringState::kInNestedExpression,
            ContainerTree({StringParentContinuationTree({NewTree(
                ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"more nested content"),
                kNestedContentModifiers, {}, {})})})),

        TestParseQuotedString(
            L"MultiLine_Nested_ContinuationInNestedClosesNested",
            L"nested_end} string continues", L'\"', kContentModifiers,
            kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept,
            CurrentState::kContinuationInNestedExpression,
            ParseQuotedStringState::kInDefaultState,
            ContainerTree({StringParentTree(
                ColumnNumber{0},
                {NewTree(ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"nested_end"),
                         kNestedContentModifiers, {}, {}),
                 NewTree(ColumnNumber{10}, kNestedSyntaxSuffix.read(),
                         kNestedPrefixSuffixModifiers, {}, {}),
                 NewTree(ColumnNumber{11},
                         SINGLE_LINE_CONSTANT(L" string continues"),
                         kContentModifiers, kContentProperties, {})})})),

        TestParseQuotedString(
            L"MultiLine_Nested_ContinuationInDefaultStartsInNested",
            L"default content {expr_start", L'\"', kContentModifiers,
            kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept, CurrentState::kContinuationInDefault,
            ParseQuotedStringState::kInNestedExpression,
            ContainerTree({StringParentContinuationTree(
                {NewTree(ColumnNumber{0},
                         SINGLE_LINE_CONSTANT(L"default content "),
                         kContentModifiers, kContentProperties, {}),
                 NewTree(ColumnNumber{16}, kNestedSyntaxPrefix.read(),
                         kNestedPrefixSuffixModifiers, {}, {}),
                 NewTree(ColumnNumber{17}, SINGLE_LINE_CONSTANT(L"expr_start"),
                         kNestedContentModifiers, {}, {})})})),

        TestParseQuotedString(
            L"MultiLine_Nested_ContinuationInDefaultFullNested",
            L"default {expr_full} more default", L'\"', kContentModifiers,
            kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept, CurrentState::kContinuationInDefault,
            ParseQuotedStringState::kInDefaultState,
            ContainerTree({StringParentContinuationTree(
                {NewTree(ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"default "),
                         kContentModifiers, kContentProperties, {}),
                 NewTree(ColumnNumber{8}, kNestedSyntaxPrefix.read(),
                         kNestedPrefixSuffixModifiers, {}, {}),
                 NewTree(ColumnNumber{9}, SINGLE_LINE_CONSTANT(L"expr_full"),
                         kNestedContentModifiers, {}, {}),
                 NewTree(ColumnNumber{18}, kNestedSyntaxSuffix.read(),
                         kNestedPrefixSuffixModifiers, {}, {}),
                 NewTree(ColumnNumber{19},
                         SINGLE_LINE_CONSTANT(L" more default"),
                         kContentModifiers, kContentProperties, {})})})),

        TestParseQuotedString(
            L"MultiLine_Nested_ContinuationInDefaultFullNestedFinishes",
            L"default {expr_full} final\"", L'\"', kContentModifiers,
            kContentProperties,
            NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = kNestedPrefixSuffixModifiers,
                .modifiers = kNestedContentModifiers},
            MultipleLinesSupport::kAccept, CurrentState::kContinuationInDefault,
            ParseQuotedStringState::kDone,
            ContainerTree(
                {NewTree(
                     ColumnNumber{0},
                     SINGLE_LINE_CONSTANT(L"default {expr_full} final"), {}, {},
                     {
                         NewTree(ColumnNumber{0},
                                 SINGLE_LINE_CONSTANT(L"default "),
                                 kContentModifiers, kContentProperties, {}),
                         NewTree(ColumnNumber{8}, kNestedSyntaxPrefix.read(),
                                 kNestedPrefixSuffixModifiers, {}, {}),
                         NewTree(ColumnNumber{9},
                                 SINGLE_LINE_CONSTANT(L"expr_full"),
                                 kNestedContentModifiers, {}, {}),
                         NewTree(ColumnNumber{18}, kNestedSyntaxSuffix.read(),
                                 kNestedPrefixSuffixModifiers, {}, {}),
                         NewTree(ColumnNumber{19},
                                 SINGLE_LINE_CONSTANT(L" final"),
                                 kContentModifiers, kContentProperties, {}),
                     }),
                 ClosingQuote(ColumnNumber{25})})),
    });

}  // namespace afc::editor::parsers
