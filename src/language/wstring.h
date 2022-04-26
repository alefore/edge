#ifndef __AFC_EDITOR_WSTRING_H__
#define __AFC_EDITOR_WSTRING_H__

#include <wchar.h>

#include <iostream>
#include <string>
#include <vector>

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

namespace afc::language {
using ::operator<<;
std::string ToByteString(std::wstring input);
std::wstring FromByteString(std::string input);
std::wstring ShellEscape(std::wstring input);
}  // namespace afc::language

#endif  // __AFC_EDITOR_WSTRING_H__
