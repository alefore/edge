#include "wstring.h"

#include <wchar.h>
#include <iostream>
#include <string>
#include <vector>

namespace afc {
namespace vm {

using std::string;
using std::wstring;

string ToByteString(wstring input) {
  static std::locale loc("en_US.utf-8");

  const wchar_t* src = input.data();
  int length = wcsnrtombs(nullptr, &src, input.length(), 0, nullptr);
  if (length == -1) {
    return "<bad conversion>";
  }
  std::vector<char> output(length);

  src = input.data();
  wcsnrtombs(&output[0], &src, input.length(), output.size(), nullptr);

  std::string output_string(&output[0], output.size());
  return output_string;
}

wstring FromByteString(string input) {
  const char* src = input.data();
  int length = mbsnrtowcs(nullptr, &src, input.length(), 0, nullptr);
  if (length == -1) {
    return L"<bad conversion>";
  }
  std::vector<wchar_t> output(length);

  src = input.data();
  mbsnrtowcs(&output[0], &src, input.length(), output.size(), nullptr);

  std::wstring output_string(&output[0], output.size());
  return output_string;
}

}  // namespace vm
}  // namespace afc
