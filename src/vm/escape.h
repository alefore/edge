#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <map>
#include <optional>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"
#include "src/vm/types.h"

namespace afc::vm {
class EscapedString {
 public:
  static EscapedString FromString(language::lazy_string::LazyString input);
  explicit EscapedString(language::text::LineSequence input);

  static language::ValueOrError<EscapedString> Parse(
      language::lazy_string::LazyString input);

  language::lazy_string::SingleLine EscapedRepresentation() const;
  language::lazy_string::SingleLine CppRepresentation() const;

  // Returns the original (unescaped) string.
  language::lazy_string::LazyString OriginalString() const;

 private:
  EscapedString(language::lazy_string::LazyString original_string);

  // The original (unescaped) string.
  language::lazy_string::LazyString input_;
};

class EscapedMap {
 public:
  using Map = std::multimap<Identifier, language::lazy_string::LazyString>;

 private:
  Map input_;

 public:
  explicit EscapedMap(Map input);

  static language::ValueOrError<EscapedMap> Parse(
      language::lazy_string::LazyString input);

  language::lazy_string::LazyString Serialize() const;

  const Map& read() const;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
