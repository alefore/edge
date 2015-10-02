#ifndef __AFC_EDITOR_VM_INTERNAL_WSTRING_H__
#define __AFC_EDITOR_VM_INTERNAL_WSTRING_H__

#include <iostream>
#include <string>
#include <vector>
#include <wchar.h>

namespace afc {
namespace vm {

using std::string;
using std::wstring;

inline std::ostream& operator<<(std::ostream& out, const wchar_t* str) {
  size_t len = 1 + std::wcsrtombs(nullptr, &str, 0, nullptr);
  std::vector<char> mbstr(len);
  std::wcsrtombs(&mbstr[0], &str, mbstr.size(), nullptr);
  out << &mbstr[0];
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const std::wstring& str) {
  return operator<<(out, str.c_str());
}

} // namespace vm
} // namespace afc

#endif // __AFC_EDITOR_VM_INTERNAL_WSTRING_H__
