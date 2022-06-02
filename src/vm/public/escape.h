#ifndef __AFC_VM_PUBLIC_ESCAPE_H__
#define __AFC_VM_PUBLIC_ESCAPE_H__

#include <optional>
#include <string>

namespace afc::vm {
std::wstring CppEscapeString(std::wstring input);
std::optional<std::wstring> CppUnescapeString(std::wstring input);
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_ESCAPE_H__
