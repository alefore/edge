#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <optional>
#include <string>

#include "src/language/value_or_error.h"

namespace afc::vm {
class CppString {
 public:
  static CppString FromString(std::wstring input);
  static language::ValueOrError<CppString> FromEscapedString(
      std::wstring input);

  // Returns an escaped representation.
  std::wstring Escape() const;

  // Returns the original (unescaped) string.
  std::wstring OriginalString() const;

 private:
  CppString(std::wstring);

  // The original (unescaped) string.
  std::wstring input_;
};
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
