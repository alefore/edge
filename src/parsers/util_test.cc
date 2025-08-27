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

namespace afc::editor {

using ::operator<<;

tests::Test TestParseQuotedString(
    const std::wstring& name, const std::wstring& input_string,
    wchar_t quote_char, const LineModifierSet& kContentModifiers,
    const std::unordered_set<ParseTreeProperty>& kContentProperties,
    std::optional<parsers::NestedExpressionSyntax> nested_expression_syntax,
    ParseTree expected_tree) {
  return tests::Test{
      .name = name, .callback = [=] {
        LineSequence contents = LineSequence::ForTests({input_string});
        ParseData parse_data(contents, {}, LineColumn{LineNumber{1}});

        std::vector<ParseTree> parse_trees = {ParseTree(Range(
            LineColumn{LineNumber{0}, ColumnNumber{0}},
            LineColumn{LineNumber{0}, ColumnNumber{input_string.size()}}))};

        // Skip opening quote, as expected by ParseQuotedString.
        parse_data.seek().Once();

        parsers::ParseQuotedStringState state = parsers::ParseQuotedString(
            &parse_data, quote_char, kContentModifiers, kContentProperties,
            nested_expression_syntax, parsers::MultipleLinesSupport::kReject,
            parsers::CurrentState::kStart);
        CHECK_EQ(state, parsers::ParseQuotedStringState::kDone);

        for (const auto& action : parse_data.parse_results().actions)
          Execute(action, &parse_trees, LineNumber{0});
        CHECK_EQ(parse_trees[0], expected_tree);
      }};
}

// Helper function to create a parse tree node and append it to a parent tree.
ParseTree NewTree(ColumnNumber begin, SingleLine contents,
                  LineModifierSet modifiers,
                  std::unordered_set<ParseTreeProperty> properties,
                  std::vector<ParseTree> children) {
  ParseTree output(Range(LineColumn(LineNumber{0}, begin),
                         LineColumn(LineNumber{0}, begin + contents.size())));
  output.set_modifiers(modifiers);
  output.set_properties(properties);
  for (ParseTree& c : children) output.PushChild(c);
  return output;
}

static const SingleLine kOpeningQuote = SINGLE_LINE_CONSTANT(L"\"");
static const SingleLine kClosingQuote = SINGLE_LINE_CONSTANT(L"\"");
static const NonEmptySingleLine kNestedSyntaxPrefix =
    NON_EMPTY_SINGLE_LINE_CONSTANT(L"{");
static const NonEmptySingleLine kNestedSyntaxSuffix =
    NON_EMPTY_SINGLE_LINE_CONSTANT(L"}");
static const LineModifierSet kContentModifiers = {LineModifier::kBold};
static const std::unordered_set<ParseTreeProperty> kContentProperties = {
    ParseTreeProperty::StringValue()};
LineModifierSet nested_prefix_suffix_modifiers = {LineModifier::kDim};
LineModifierSet kNestedContentModifiers = {LineModifier::kGreen};

bool parse_quoted_string_tests = afc::tests::Register(
    L"ParseQuotedString",
    std::vector<afc::tests::Test>{
        TestParseQuotedString(
            L"EmptyString", L"\"\"", L'\"', kContentModifiers,
            kContentProperties, std::nullopt,
            NewTree(ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"\"\""), {}, {},
                    {NewTree(ColumnNumber{0}, kOpeningQuote,
                             LineModifierSet{LineModifier::kDim}, {}, {}),
                     NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L""), {}, {},
                             {}),
                     NewTree(ColumnNumber{1}, kClosingQuote,
                             LineModifierSet{LineModifier::kDim}, {}, {})})),
        TestParseQuotedString(
            L"SimpleString", L"\"hello\"", L'\"', kContentModifiers,
            kContentProperties, std::nullopt,
            NewTree(
                ColumnNumber{0}, SINGLE_LINE_CONSTANT(L"\"hello\""), {}, {},
                {NewTree(ColumnNumber{0}, kOpeningQuote,
                         LineModifierSet{LineModifier::kDim}, {}, {}),
                 NewTree(
                     ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"hello"), {}, {},
                     {NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"hello"),
                              kContentModifiers, kContentProperties, {})}),
                 NewTree(ColumnNumber{6}, kClosingQuote,
                         LineModifierSet{LineModifier::kDim}, {}, {})})),
        TestParseQuotedString(
            L"NestedExpressionString", L"\"foo {expr} bar\"", L'\"',
            kContentModifiers, kContentProperties,
            parsers::NestedExpressionSyntax{
                .prefix = kNestedSyntaxPrefix,
                .suffix = kNestedSyntaxSuffix,
                .prefix_suffix_modifiers = nested_prefix_suffix_modifiers,
                .modifiers = kNestedContentModifiers},
            NewTree(
                ColumnNumber{0},
                SINGLE_LINE_CONSTANT(L"\"foo {expr} bar\""), {}, {},
                {NewTree(ColumnNumber{0}, kOpeningQuote,
                         LineModifierSet{LineModifier::kDim}, {}, {}),
                 NewTree(
                     ColumnNumber{1},
                     SINGLE_LINE_CONSTANT(L"foo {expr} bar"), {}, {},
                     {
                         NewTree(ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"foo "),
                                 kContentModifiers, kContentProperties, {}),
                         NewTree(ColumnNumber{5}, kNestedSyntaxPrefix.read(),
                                 nested_prefix_suffix_modifiers, {}, {}),
                         NewTree(ColumnNumber{6}, SINGLE_LINE_CONSTANT(L"expr"),
                                 kNestedContentModifiers, {}, {}),
                         NewTree(ColumnNumber{10}, kNestedSyntaxSuffix.read(),
                                 nested_prefix_suffix_modifiers, {}, {}),
                         NewTree(ColumnNumber{11},
                                 SINGLE_LINE_CONSTANT(L" bar"),
                                 kContentModifiers, kContentProperties, {}),
                     }),
                 NewTree(ColumnNumber{15}, kClosingQuote,
                         LineModifierSet{LineModifier::kDim}, {}, {})})),
        TestParseQuotedString(
            L"MultipleNestedExpressions",
            L"\"foo {i + 1} bar quux {some.call()} nah\"", L'\"',
            kContentModifiers, kContentProperties,
            parsers::NestedExpressionSyntax{
                .prefix = NON_EMPTY_SINGLE_LINE_CONSTANT(L"{"),
                .suffix = NON_EMPTY_SINGLE_LINE_CONSTANT(L"}"),
                .prefix_suffix_modifiers = nested_prefix_suffix_modifiers,
                .modifiers = kNestedContentModifiers},
            NewTree(ColumnNumber{0},
                    SINGLE_LINE_CONSTANT(
                        L"\"foo {i + 1} bar quux {some.call()} nah\""),
                    {}, {},
                    {NewTree(ColumnNumber{0}, kOpeningQuote,
                             LineModifierSet{LineModifier::kDim}, {}, {}),
                     NewTree(
                         ColumnNumber{1},
                         SINGLE_LINE_CONSTANT(
                             L"foo {i + 1} bar quux {some.call()} nah"),
                         {},
                         {},
                         {
                             NewTree(
                                 ColumnNumber{1}, SINGLE_LINE_CONSTANT(L"foo "),
                                 kContentModifiers, kContentProperties, {}),
                             NewTree(ColumnNumber{5},
                                     kNestedSyntaxPrefix.read(),
                                     nested_prefix_suffix_modifiers, {}, {}),
                             NewTree(ColumnNumber{6},
                                     SINGLE_LINE_CONSTANT(L"i + 1"),
                                     kNestedContentModifiers, {}, {}),
                             NewTree(ColumnNumber{11},
                                     kNestedSyntaxSuffix.read(),
                                     nested_prefix_suffix_modifiers, {}, {}),
                             NewTree(ColumnNumber{12},
                                     SINGLE_LINE_CONSTANT(L" bar quux "),
                                     kContentModifiers, kContentProperties, {}),
                             NewTree(ColumnNumber{22},
                                     kNestedSyntaxPrefix.read(),
                                     nested_prefix_suffix_modifiers, {}, {}),
                             NewTree(ColumnNumber{23},
                                     SINGLE_LINE_CONSTANT(L"some.call()"),
                                     kNestedContentModifiers, {}, {}),
                             NewTree(ColumnNumber{34},
                                     kNestedSyntaxSuffix.read(),
                                     nested_prefix_suffix_modifiers, {}, {}),
                             NewTree(ColumnNumber{35},
                                     SINGLE_LINE_CONSTANT(L" nah"),
                                     kContentModifiers, kContentProperties, {}),
                         }),
                     NewTree(ColumnNumber{39}, kClosingQuote,
                             LineModifierSet{LineModifier::kDim}, {}, {})}))});

}  // namespace afc::editor
