#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <map>
#include <optional>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"
#include "src/vm/types.h"

namespace afc::vm {
class EscapedString
    : public language::GhostType<EscapedString,
                                 language::lazy_string::LazyString> {
 public:
  using GhostType::GhostType;

  static EscapedString FromString(language::lazy_string::LazyString input);

  static language::ValueOrError<EscapedString> Parse(
      language::lazy_string::LazyString input);
  static language::ValueOrError<EscapedString> ParseURL(
      language::lazy_string::SingleLine input);

  language::lazy_string::SingleLine EscapedRepresentation() const;
  language::lazy_string::NonEmptySingleLine CppRepresentation() const;
  language::lazy_string::SingleLine URLRepresentation() const;

  // Returns the original (unescaped) string.
  language::lazy_string::LazyString OriginalString() const;
};

class EscapedMap {
 public:
  using Map = std::multimap<Identifier, EscapedString>;

 private:
  Map input_;

 public:
  explicit EscapedMap(Map input);

  static language::ValueOrError<EscapedMap> Parse(
      language::lazy_string::SingleLine input);

  language::lazy_string::SingleLine Serialize() const;

  const Map& read() const;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
