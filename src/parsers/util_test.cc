#include "src/parsers/util.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/single_line.h"  // For lazy_string::SingleLine
#include "src/language/text/line.h"                // For Line
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/range.h"
#include "src/parse_tools.h"
#include "src/parse_tree.h"
#include "src/tests/tests.h"

// Top-level using statements for afc::language and afc::infrastructure as per
// style guide.
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor {

using ::operator<<;

ParseData CreateParseData(const LineSequence& content) {
  return ParseData(content, {}, LineColumn{LineNumber{1}});
}

struct ParseResultForTest {
  ParseData parse_data;
  ParseTree root_tree;
};

// Helper function to parse a quoted string and return the resulting ParseTree.
// This helper initializes the parse_trees vector with a root ParseTree covering
// the entire input range, as expected by the Execute function.
// Renamed from ParseQuotedStringAndGetTree to TestParseQuotedString
ParseResultForTest TestParseQuotedString(
    const std::wstring& input_string, wchar_t quote_char,
    const LineModifierSet& content_modifiers,
    const std::unordered_set<ParseTreeProperty>& content_properties) {
  LineSequence contents = LineSequence::ForTests({input_string});
  ParseData parse_data = CreateParseData(contents);

  // Get the content string length from parse_data's buffer.
  // Assuming single line for these tests (LineNumber{0}).
  SingleLine single_line = parse_data.buffer().at(LineNumber{0}).contents();
  ColumnNumberDelta line_length = single_line.size();

  LineColumn start_range = LineColumn{LineNumber{0}, ColumnNumber{0}};
  LineColumn end_range =
      LineColumn{LineNumber{0}, ColumnNumber{} + line_length};

  std::vector<ParseTree> parse_trees = {
      ParseTree(Range(start_range, end_range))};

  parse_data.seek().Once();

  parsers::ParseQuotedString(&parse_data, quote_char, content_modifiers,
                             content_properties, std::nullopt,
                             parsers::MultipleLinesSupport::kReject,
                             parsers::CurrentState::kStart);

  for (const auto& action : parse_data.parse_results().actions) {
    Execute(action, &parse_trees, LineNumber{0});
  }
  return ParseResultForTest{std::move(parse_data), parse_trees[0]};
}

struct QuotedStringParseExpectations {
  ColumnNumber expected_final_column;
  ColumnNumber expected_root_begin_column;
  ColumnNumber expected_root_end_column;
  size_t expected_root_children_size;

  ColumnNumber expected_open_quote_begin_column;
  ColumnNumber expected_open_quote_end_column;

  ColumnNumber expected_content_begin_column;
  ColumnNumber expected_content_end_column;
  LineModifierSet expected_content_modifiers;
  std::unordered_set<ParseTreeProperty> expected_content_properties;
  std::optional<size_t> expected_content_children_size;

  ColumnNumber expected_close_quote_begin_column;
  ColumnNumber expected_close_quote_end_column;

  void Validate(const ParseResultForTest& result) const {
    CHECK_EQ(result.parse_data.position().column, expected_final_column);
    CHECK_EQ(result.root_tree.range().begin().column,
             expected_root_begin_column);
    CHECK_EQ(result.root_tree.range().end().column, expected_root_end_column);
    CHECK_EQ(result.root_tree.children().size(), expected_root_children_size);

    // Child 1: Opening Quote
    CHECK_EQ(result.root_tree.children()[0].range().begin().column,
             expected_open_quote_begin_column);
    CHECK_EQ(result.root_tree.children()[0].range().end().column,
             expected_open_quote_end_column);
    CHECK_EQ(result.root_tree.children()[0].modifiers(),
             LineModifierSet{LineModifier::kDim});
    CHECK(result.root_tree.children()[0].properties().empty());

    // Child 2: Content
    CHECK_EQ(result.root_tree.children()[1].range().begin().column,
             expected_content_begin_column);
    CHECK_EQ(result.root_tree.children()[1].range().end().column,
             expected_content_end_column);
    if (expected_content_children_size.has_value()) {
      CHECK_EQ(result.root_tree.children()[1].children().size(),
               expected_content_children_size.value());
      // If there's a nested child, check its properties and modifiers
      const ParseTree& nested_content_child =
          result.root_tree.children()[1].children()[0];
      CHECK_EQ(nested_content_child.range().begin().column,
               expected_content_begin_column);
      CHECK_EQ(nested_content_child.range().end().column,
               expected_content_end_column);
      CHECK_EQ(nested_content_child.modifiers(), expected_content_modifiers);
      CHECK_EQ(nested_content_child.properties().size(),
               expected_content_properties.size());
      for (const auto& prop : expected_content_properties) {
        CHECK(nested_content_child.properties().count(prop));
      }
    } else {
      CHECK(result.root_tree.children()[1].modifiers().empty());
      CHECK(result.root_tree.children()[1].properties().empty());
      CHECK(result.root_tree.children()[1].children().empty());
    }

    // Child 3: Closing Quote
    CHECK_EQ(result.root_tree.children()[2].range().begin().column,
             expected_close_quote_begin_column);
    CHECK_EQ(result.root_tree.children()[2].range().end().column,
             expected_close_quote_end_column);
    CHECK_EQ(result.root_tree.children()[2].modifiers(),
             LineModifierSet{LineModifier::kDim});
    CHECK(result.root_tree.children()[2].properties().empty());
  }
};

bool parse_quoted_string_tests = afc::tests::Register(L"ParseQuotedString", [] {
  return std::vector<afc::tests::Test>(
      {{.name = L"EmptyString",
        .callback =
            [] {
              LineModifierSet modifiers = {};
              std::unordered_set<ParseTreeProperty> properties = {};

              ParseResultForTest result =
                  TestParseQuotedString(L"\"\"", L'\"', modifiers, properties);

              QuotedStringParseExpectations expectations = {
                  .expected_final_column = ColumnNumber{2},
                  .expected_root_begin_column = ColumnNumber{0},
                  .expected_root_end_column = ColumnNumber{2},
                  .expected_root_children_size = 3,
                  .expected_open_quote_begin_column = ColumnNumber{0},
                  .expected_open_quote_end_column = ColumnNumber{1},
                  .expected_content_begin_column = ColumnNumber{1},
                  .expected_content_end_column = ColumnNumber{1},
                  .expected_content_modifiers = {},
                  .expected_content_properties = {},
                  .expected_content_children_size = std::nullopt,
                  .expected_close_quote_begin_column = ColumnNumber{1},
                  .expected_close_quote_end_column = ColumnNumber{2},
              };
              expectations.Validate(result);
            }},
       {.name = L"SimpleString", .callback = [] {
          LineModifierSet modifiers = {LineModifier::kBold};
          std::unordered_set<ParseTreeProperty> properties = {
              ParseTreeProperty::StringValue()};

          ParseResultForTest result =
              TestParseQuotedString(L"\"hello\"", L'\"', modifiers, properties);

          QuotedStringParseExpectations expectations = {
              .expected_final_column = ColumnNumber{7},
              .expected_root_begin_column = ColumnNumber{0},
              .expected_root_end_column = ColumnNumber{7},
              .expected_root_children_size = 3,
              .expected_open_quote_begin_column = ColumnNumber{0},
              .expected_open_quote_end_column = ColumnNumber{1},
              .expected_content_begin_column = ColumnNumber{1},
              .expected_content_end_column = ColumnNumber{6},
              .expected_content_modifiers = modifiers,
              .expected_content_properties = properties,
              .expected_content_children_size = 1,
              .expected_close_quote_begin_column = ColumnNumber{6},
              .expected_close_quote_end_column = ColumnNumber{7},
          };
          expectations.Validate(result);
        }}});
}());

}  // namespace afc::editor
