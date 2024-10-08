#ifndef __AFC_EDITOR_BUFFER_FILTER_H__
#define __AFC_EDITOR_BUFFER_FILTER_H__

#include <vector>

#include "src/futures/delete_notification.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/vm/escape.h"
#include "src/vm/types.h"

namespace afc::editor {
const vm::Identifier& HistoryIdentifierValue();
const vm::Identifier& HistoryIdentifierExtension();
const vm::Identifier& HistoryIdentifierName();
const vm::Identifier& HistoryIdentifierActive();
const vm::Identifier& HistoryIdentifierDirectory();

struct TokenAndModifiers {
  // The portion to colorize. The `value` field is ignored; instead, the
  // corresponding portion from `line` will be used.
  language::lazy_string::Token token;
  // Set of modifiers to apply.
  infrastructure::screen::LineModifierSet modifiers;
};

language::text::Line ColorizeLine(language::lazy_string::LazyString line,
                                  std::vector<TokenAndModifiers> tokens);

struct FilterSortBufferInput {
  futures::DeleteNotification::Value abort_value;
  language::lazy_string::SingleLine filter;
  language::text::LineSequence history;
  std::multimap<vm::Identifier, vm::EscapedString> current_features;
};

struct FilterSortBufferOutput {
  std::vector<language::Error> errors;

  struct Match {
    language::text::Line preview;
    language::text::LineSequence data;
  };
  std::vector<Match> matches;
};

FilterSortBufferOutput FilterSortBuffer(FilterSortBufferInput input);

bool operator==(const FilterSortBufferOutput::Match&,
                const FilterSortBufferOutput::Match&);
std::ostream& operator<<(std::ostream& os,
                         const FilterSortBufferOutput::Match&);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_FILTER_H__
