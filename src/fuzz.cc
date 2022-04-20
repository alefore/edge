#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_contents.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"

namespace afc {
namespace editor {
namespace fuzz {
using language::FromByteString;

std::optional<size_t> Reader<size_t>::Read(Stream& input_stream) {
  size_t high = abs(input_stream.get());
  size_t low = abs(input_stream.get());
  return input_stream.eof() ? std::optional<size_t>() : (high << 8) + low;
}

std::optional<ShortRandomLine> Reader<ShortRandomLine>::Read(
    Stream& input_stream) {
  char len = input_stream.get();
  char buffer[256];
  std::string output;
  if (!input_stream.getline(buffer, std::min(256, static_cast<int>(len)))) {
    return std::nullopt;
  }
  return ShortRandomLine{FromByteString(output)};
}

std::optional<ShortRandomString> Reader<ShortRandomString>::Read(
    Stream& input_stream) {
  size_t len = static_cast<unsigned char>(input_stream.get());
  CHECK_LE(len, 256ul);
  char buffer[256];
  for (size_t i = 0; i < len; i++) {
    buffer[i] = input_stream.get();
  }
  return input_stream.eof()
             ? std::optional<ShortRandomString>()
             : ShortRandomString{FromByteString(string(buffer, len))};
}

Handler Call(std::function<void()> callback) {
  return [callback](std::ifstream&) { callback(); };
}

}  // namespace fuzz
}  // namespace editor
}  // namespace afc
