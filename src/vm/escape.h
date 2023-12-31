#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <optional>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::vm {
class EscapedString {
 public:
  static EscapedString FromString(language::lazy_string::LazyString input);

  static language::ValueOrError<EscapedString> Parse(
      language::lazy_string::LazyString input);

  std::wstring EscapedRepresentation() const;
  std::wstring CppRepresentation() const;

  // Returns the original (unescaped) string.
  language::lazy_string::LazyString OriginalString() const;

 private:
  EscapedString(language::lazy_string::LazyString original_string);

  // The original (unescaped) string.
  language::lazy_string::LazyString input_;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
