#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <optional>
#include <string>

#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/errors/value_or_error.h"

namespace afc::vm {
class EscapedString {
 public:
  static EscapedString FromString(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
          input);

  static language::ValueOrError<EscapedString> Parse(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
          input);

  std::wstring EscapedRepresentation() const;
  std::wstring CppRepresentation() const;

  // Returns the original (unescaped) string.
  language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  OriginalString() const;

 private:
  EscapedString(std::wstring original_string);

  // The original (unescaped) string.
  std::wstring input_;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
