#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <optional>
#include <string>

#include "src/language/ghost_type.h"
#include "src/language/value_or_error.h"

namespace afc::vm {
class EscapedString {
 public:
  static EscapedString FromString(std::wstring input);

  static language::ValueOrError<EscapedString> Parse(std::wstring input);

  std::wstring EscapedRepresentation() const;
  std::wstring CppRepresentation() const;

  // Returns the original (unescaped) string.
  std::wstring OriginalString() const;

 private:
  EscapedString(std::wstring original_string);

  // The original (unescaped) string.
  std::wstring input_;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
