#include <iostream>

#include "line_parser.h"
#include "token.h"

namespace afc {
namespace editor {

std::unique_ptr<Token> Parse(const std::string& contents) {
  std::unique_ptr<Token> output(new Token(""));
  size_t position = 0;
  while (position < contents.length()) {
    size_t line_end = contents.find('\n', position);
    if (line_end != std::string::npos) {
      line_end++;
    }
    output->AppendToken(
        std::unique_ptr<Token>(new Token(contents.substr(position, line_end - position))));
    position = line_end;
  }
  return std::move(output);
}

}  // namespace editor
}  // namespace afc
