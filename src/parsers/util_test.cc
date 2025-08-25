#include "src/parsers/util.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/tests/tests.h"

// Top-level using statements for afc::language and afc::infrastructure as per
// style guide.
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;

namespace afc::editor {

using ::operator<<;

ParseData CreateParseData(const LineSequence& content) {
  return ParseData(content, {}, LineColumn{LineNumber{1}});
}

bool parse_quoted_string_tests = afc::tests::Register(L"ParseQuotedString", [] {
  return std::vector<afc::tests::Test>(
      {{.name = L"EmptyString",
        .callback =
            [] {
              LineSequence contents = LineSequence::ForTests({L"\"\""});
              ParseData parse_data = CreateParseData(contents);
              parse_data.seek().Once();
              parsers::ParseQuotedStringState state =
                  parsers::ParseQuotedString(
                      &parse_data, L'"', LineModifierSet{},
                      std::unordered_set<ParseTreeProperty>{}, std::nullopt,
                      parsers::MultipleLinesSupport::kReject,
                      parsers::CurrentState::kStart);

              CHECK_EQ(
                  static_cast<int>(state),
                  static_cast<int>(parsers::ParseQuotedStringState::kDone));
              CHECK_EQ(parse_data.position().column, ColumnNumber{2});
            }},
       {.name = L"SimpleString", .callback = [] {
          LineSequence contents = LineSequence::ForTests({L"\"hello\""});
          ParseData parse_data = CreateParseData(contents);
          parse_data.seek().Once();

          parsers::ParseQuotedStringState state = parsers::ParseQuotedString(
              &parse_data, L'"', LineModifierSet{},
              std::unordered_set<ParseTreeProperty>{}, std::nullopt,
              parsers::MultipleLinesSupport::kReject,
              parsers::CurrentState::kStart);

          CHECK_EQ(static_cast<int>(state),
                   static_cast<int>(parsers::ParseQuotedStringState::kDone));
          CHECK_EQ(parse_data.position().column, ColumnNumber{7});
        }}});
}());

}  // namespace afc::editor
