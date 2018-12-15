#include "src/wstring.h"

#include <locale>
#include <vector>

#include <glog/logging.h>

namespace afc {
namespace editor {

typedef std::codecvt<wchar_t, char, std::mbstate_t> Converter;

string ToByteString(wstring input) {
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

wstring FromByteString(string input) {
  VLOG(5) << "FromByteString: " << input;

  const char* src = input.data();
  int length = mbsnrtowcs(nullptr, &src, input.length(), 0, nullptr);
  VLOG(6) << "Output length: " << length;
  if (length == -1) {
    return L"<bad conversion>";
  }
  std::vector<wchar_t> output(length);

  src = input.data();
  mbsnrtowcs(&output[0], &src, input.length(), output.size(), nullptr);

  std::wstring output_string(&output[0], output.size());
  VLOG(6) << "Conversion result: [" << output_string << "]";
  return output_string;
}

}  // namespace editor
}  // namespace afc
