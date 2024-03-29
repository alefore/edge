#ifndef __AFC_EDITOR_PARSERS_UTIL_H__
#define __AFC_EDITOR_PARSERS_UTIL_H__

#include "src/lru_cache.h"
#include "src/parse_tools.h"

namespace afc::editor::parsers {
// `result` should be after the initial double-quoted string.
void ParseDoubleQuotedString(
    ParseData* result, infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties);

// `result` should be after the initial digit.
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
