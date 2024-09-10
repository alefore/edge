#ifndef __AFC_LANGUAGE_LAZY_STRING_SINGLE_LINE_H__
#define __AFC_LANGUAGE_LAZY_STRING_SINGLE_LINE_H__

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::lazy_string {
struct SingleLineValidator {
  static language::PossibleError Validate(const LazyString& input);
};

class SingleLine
    : public GhostType<SingleLine, LazyString, SingleLineValidator> {
 public:
  SingleLine Substring(ColumnNumber) const;
  SingleLine Substring(ColumnNumber, ColumnNumberDelta) const;
  SingleLine Append(SingleLine) const;
};

SingleLine operator+(const SingleLine& a, const SingleLine& b);
}  // namespace afc::language::lazy_string

#endif
