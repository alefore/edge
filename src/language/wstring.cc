#include "src/language/wstring.h"

#include <glog/logging.h>

#include <locale>
#include <vector>

namespace afc::language {

typedef std::codecvt<wchar_t, char, std::mbstate_t> Converter;

std::string ToByteString(std::wstring input) {
  VLOG(5) << "ToByteString: " << input;

  static std::locale loc("en_US.utf-8");

  const wchar_t* src = input.data();
  int length = wcsnrtombs(nullptr, &src, input.length(), 0, nullptr);
  VLOG(6) << "Output length: " << length;
  if (length == -1) {
    return "<bad conversion>";
  }
  std::vector<char> output(length);

  src = input.data();
  wcsnrtombs(&output[0], &src, input.length(), output.size(), nullptr);

  std::string output_string(&output[0], output.size());
  VLOG(6) << "Conversion result: [" << output_string << "]";
  return output_string;
}

std::wstring FromByteString(std::string input) {
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

// TODO(easy, 2022-06-05): Deduplicate this against EscapedString.
std::wstring ShellEscape(std::wstring input) {
  std::wstring output;
  output.push_back(L'\'');
  for (auto c : input) {
    if (c == L'\'') {
      output.push_back('\\');
    }
    output.push_back(c);
  }
  output.push_back(L'\'');
  return output;
}
}  // namespace afc::language
