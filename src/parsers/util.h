#ifndef __AFC_EDITOR_PARSERS_UTIL_H__
#define __AFC_EDITOR_PARSERS_UTIL_H__

#include <ostream>  // For operator<< overload

#include "src/language/lazy_string/single_line.h"
#include "src/lru_cache.h"
#include "src/parse_tools.h"

namespace afc::editor::parsers {

// What should be done if the line finishes before the closing quote_char is
// found?
enum class MultipleLinesSupport { kAccept, kReject };

// What's the current state of the parser (for multi-line strings)?
enum class CurrentState {
  // Very first call (at the start of the quoted line).
  kStart,
  // Continuing a quoted string in a new line, not inside a nested expression.
  kContinuationInDefault,
  // Continuing a quoted string in a new line, inside a nested expression.
  kContinuationInNestedExpression
};

// What's the state of the parser (when `ParseQuotedString` returns)?
enum class ParseQuotedStringState {
  // The quoted string was fully consumed (including `quote_char`).
  kDone,
  // The quoted string continues in the next line, in the default state.
  // Only returned when MultipleLinesSupport::kAccept.
  kInDefaultState,
  // The quoted string continues in the next line, inside a nested expression.
  // Only returned when MultipleLinesSupport::kAccept.
  kInNestedExpression
};

inline std::ostream& operator<<(std::ostream& os,
                                const ParseQuotedStringState& state) {
  switch (state) {
    case ParseQuotedStringState::kDone:
      return os << "kDone";
    case ParseQuotedStringState::kInDefaultState:
      return os << "kInDefaultState";
    case ParseQuotedStringState::kInNestedExpression:
      return os << "kInNestedExpression";
  }
  return os;  // Should not reach here
}

ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char,
    infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties);

ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char,
    infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties,
    MultipleLinesSupport multiple_lines_support, CurrentState current_state);

struct NestedExpressionSyntax {
  language::lazy_string::NonEmptySingleLine prefix;
  language::lazy_string::NonEmptySingleLine suffix;
  infrastructure::screen::LineModifierSet prefix_suffix_modifiers;
  // Applied to the string between (excluding) prefix and suffix.
  infrastructure::screen::LineModifierSet modifiers;
};

// `result` should be *immediately after* the initial `quote_char` string.
ParseQuotedStringState ParseQuotedString(
    ParseData* result, wchar_t quote_char,
    infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties,
    std::optional<NestedExpressionSyntax> nested_expression_syntax,
    MultipleLinesSupport multiple_lines_support, CurrentState current_state);

// `result` should be *immediately after* the initial digit.
void ParseNumber(ParseData* result,
                 infrastructure::screen::LineModifierSet number_modifiers,
                 std::unordered_set<ParseTreeProperty> properties);

class LineOrientedTreeParser : public TreeParser {
 public:
  ParseTree FindChildren(const language::text::LineSequence& buffer,
                         language::text::Range range);

 protected:
  static constexpr size_t kDefaultState = 0;
  virtual void ParseLine(ParseData* result) = 0;

  // Allows us to avoid reparsing previously parsed lines. The key is the hash
  // of:
  //
  // - The contents of a line.
  // - The stack of states available when parsing of the line starts.
  //
  // The values are the results of parsing the line.
  //
  // Why set the size to 1? Because `FindChildren` will adjust it to be based on
  // the size of the file.
  LRUCache<size_t, ParseResults> cache_ = LRUCache<size_t, ParseResults>(1);
};
}  // namespace afc::editor::parsers
#endif  // __AFC_EDITOR_PARSERS_UTIL_H__
