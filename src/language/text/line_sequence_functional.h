#ifndef __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_FUNCTIONAL_H__
#define __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_FUNCTIONAL_H__

#include <functional>

#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"

namespace afc::language::text {
enum class FilterPredicateResult { kKeep, kErase };

LineSequence FilterLines(
    LineSequence input,
    std::function<FilterPredicateResult(const language::text::Line&)>
        predicate);
}  // namespace afc::language::text
#endif
